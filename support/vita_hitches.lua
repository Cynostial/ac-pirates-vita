-- Vita frame-hitch fixes: work the game does in one frame, or keeps forever,
-- that the Vita turns into visible stalls. The game's own data and calls stay;
-- only when, and how often, the work happens changes.
-- Own function scope: the support files share one chunk and its 200 locals.
do local function hitches_module()
local insert=table.insert

-- Parameter registries ---------------------------------------------------------
-- Cannonballs, effects and boats register tweakable parameters with the
-- VariableTreeLoader, particle emitters 32 at a time, and most never
-- unregister: the game drops destroyed entities only while propagating a
-- changed value, which release builds never do. After one battle 6,233 of
-- 10,465 entries belonged to destroyed entities, each kept alive with all its
-- tables, and every removal scans these lists. Every two seconds of
-- registrations, drop destroyed entities exactly as UnregisterEntity would.
local PRUNE_INTERVAL=2
local pruned_at=setmetatable({},{__mode="k"})
-- Never compact a list while the game walks it to push changed values; a
-- setter it calls may create entities that register. An error inside leaves
-- the count raised, which only stops pruning.
local busy=0
local function guarded(def,key)
    local fn=def[key]
    if type(fn)~="function" then return end
    def[key]=function(...)
        busy=busy+1
        local a,b,c=fn(...)
        busy=busy-1
        return a,b,c
    end
end
local function destroyed(e) return type(e)=="table" and e.IsDestroyed==true end
local function prune(o)
    for _,entry in pairs(o.UpdatableParameterList) do
        local list=entry.Entities
        if list then
            local n,kept=#list,0
            for i=1,n do
                local v=list[i]
                if not destroyed(v.Entity) then kept=kept+1;list[kept]=v end
            end
            for i=n,kept+1,-1 do list[i]=nil end
        end
    end
    local registered=o.RegisteredEntities
    for e in pairs(registered) do
        if destroyed(e) then registered[e]=nil end
    end
end
-- Cannonballs set their mesh ("sphere") through these parameters. Its last
-- reference goes with the last ball, so every volley reloaded the geometry
-- (2.3 ms of each ball's 2.6 ms creation in the emulator, far more on the
-- Vita). Keep one reference to each such mesh for the session.
local pinned={}
local function pin_mesh(o,path)
    local known=o.UpdatableParameterList[path]
    local value=known and known.ParameterName=="Mesh" and known.ParameterPosition[known.ParameterName]
    if type(value)~="string" or pinned[value] then return end
    local state=ResourcesLoading_GetResourceLoadingState(value,"GeeaGeometry")
    if state==1 then pinned[value]=true;ResourcesLoading_AddReference(value,"GeeaGeometry",true)
    elseif state==-1 then pinned[value]=true end -- not a geometry; still loading retries
end
local function adapt_variables(def)
    local register=def.RegisterUpdatableParameter
    if type(register)~="function" then return end
    guarded(def,"_UpdatedRegisterParameter");guarded(def,"LoadData")
    local fast
    def.RegisterUpdatableParameter=function(o,path,entity,name)
        local result=fast(o,path,entity,name)
        pin_mesh(o,path)
        return result
    end
    fast=function(o,path,entity,name)
        if o.DebugActiv then return register(o,path,entity,name) end
        local now,last=AbsoluteTime or 0,pruned_at[o]
        if busy==0 and (not last or now<last or now-last>=PRUNE_INTERVAL) then pruned_at[o]=now;prune(o) end
        -- A path registered before was resolved then; the variable tree only
        -- changes in debug reloads. Skip walking it again, same effects.
        local known=o.UpdatableParameterList[path]
        if not known or type(entity)~="table" then return register(o,path,entity,name) end
        local value=known.ParameterPosition[known.ParameterName]
        if value==nil then return register(o,path,entity,name) end
        name=name or known.ParameterName
        local registered=o.RegisteredEntities
        local paths=registered[entity]
        local old=paths and paths[name]
        -- Ship parts re-register the same parameter on every camera change;
        -- the original appended a duplicate each time (1,424 entries for 89
        -- live entities after one battle). Already listed: set the value only.
        if old~=path then insert(known.Entities,{Entity=entity,EntityParameterName=name}) end
        entity[name]=value
        if not paths then paths={};registered[entity]=paths end
        if old~=path then
            if old then o:UnregisterParameter(entity,name,old) end
            paths[name]=path
        end
    end
end

-- Dialogue portraits -----------------------------------------------------------
-- A dialogue sequence loaded every portrait and background it would ever show
-- (512x512 PNGs, decoded one after another) before its first line: up to ~1.8 s
-- on the Vita. Load the opening line's images first and queue the rest with
-- the engine's asynchronous loader, which decodes them while the player reads;
-- any still pending when their line starts are finished there.
local pending_media=setmetatable({},{__mode="k"})
local function line_media(dialog)
    local names={}
    if dialog.CharacterID1~="" then names[#names+1]=dialog.CharacterID1.."_"..dialog.ExpressionCharacter1 end
    if dialog.CharacterID2~="" then names[#names+1]=dialog.CharacterID2.."_"..dialog.ExpressionCharacter2 end
    if dialog.Background~="NOBG" then names[#names+1]=dialog.Background end
    return names
end
local function adapt_dialogs(def)
    local start,unload=def._StartDialog,def._UnloadRequiredMedia
    if type(def._LoadRequiredMediaForSequence)~="function" or type(def._LoadResource)~="function" or
       type(start)~="function" or type(unload)~="function" then return end
    def._LoadRequiredMediaForSequence=function(o,sequence)
        local first={}
        if sequence[1] then for _,n in ipairs(line_media(sequence[1])) do first[n]=true end end
        local loaded=o._LoadedMedia
        local pending=pending_media[o] or {}
        pending_media[o]=pending
        for _,dialog in pairs(sequence) do
            for _,n in ipairs(line_media(dialog)) do
                if first[n] then o:_LoadResource(n)
                elseif not loaded[n] then
                    loaded[n]=true;pending[n]=true
                    ResourcesLoading_AddReference(n,"GeeaTexture",false)
                end
            end
        end
    end
    def._StartDialog=function(o,sequenceid,dialogid,...)
        local pending=pending_media[o]
        local sequence=o._Dialogs[sequenceid]
        local dialog=sequence and sequence[dialogid]
        if pending and dialog then
            local waiting={}
            for _,n in ipairs(line_media(dialog)) do
                if pending[n] then
                    pending[n]=nil
                    if ResourcesLoading_GetResourceLoadingState(n,"GeeaTexture")~=1 then waiting[#waiting+1]=n end
                end
            end
            for _=1,8 do
                if #waiting==0 then break end
                ResourcesLoading_FlushLoadingQueues(-1)
                for i=#waiting,1,-1 do
                    if ResourcesLoading_GetResourceLoadingState(waiting[i],"GeeaTexture")==1 then table.remove(waiting,i) end
                end
            end
        end
        return start(o,sequenceid,dialogid,...)
    end
    def._UnloadRequiredMedia=function(o,...)
        pending_media[o]=nil
        return unload(o,...)
    end
end

-- Saves -------------------------------------------------------------------------
-- Every crate hauled aboard saved AchievementsData three times (once per
-- reward type) and PlayerProfileData once, each writing data, .bak and .hash
-- files in that frame. Repeat requests for the same save keep only the latest
-- table, written a second after the first request, one save per frame. Reads
-- and deletes, loading screens and the pause menu write everything first.
local SAVE_DELAY=1
local pending_saves,save_order,save_due={},{},nil
local save_init=nil
local save_table,save_initialize
local function write_save(key)
    local job=pending_saves[key]
    pending_saves[key]=nil
    if not job then return end
    if job.init then save_initialize(unpack(job.init,1,job.init.n)) end
    if not save_table(unpack(job.args,1,job.args.n)) then ACPVitaLog("Deferred save failed: "..key) end
    -- Leave the storage context as the game last set it.
    if save_init and save_init~=job.init then save_initialize(unpack(save_init,1,save_init.n)) end
end
local function flush_saves()
    local order=save_order
    save_order,save_due={},nil
    for _,key in ipairs(order) do write_save(key) end
end
ACPVitaSavesFlush=function() if save_table then flush_saves() end end
local function install_saves()
    local saving,init=rawget(_G,"LuaSave_SaveTable"),rawget(_G,"LuaSave_Initialize")
    if save_table or type(saving)~="function" or type(init)~="function" then return end
    save_table,save_initialize=saving,init
    LuaSave_Initialize=function(...)
        save_init={n=select("#",...),...}
        return init(...)
    end
    LuaSave_SaveTable=function(...)
        local key=tostring((...)).."/"..tostring(select(2,...)).."/"..tostring(select(3,...))
        if not pending_saves[key] then save_order[#save_order+1]=key end
        pending_saves[key]={init=save_init,args={n=select("#",...),...}}
        save_due=save_due or (AbsoluteTime or 0)+SAVE_DELAY
        return true
    end
    for _,name in ipairs{"LuaSave_LoadTable","LuaSave_DeleteTable","LuaSave_DeleteItemsInGroup"} do
        local fn=rawget(_G,name)
        if type(fn)=="function" then _G[name]=function(...) flush_saves();return fn(...) end end
    end
end
local function saves_frame()
    install_saves()
    if not save_due then return end
    -- Paused (menu, dialogue): don't wait for the delay, still one per frame.
    local game=GetSubProcess and GetSubProcess("TagGameProcess")
    local now=AbsoluteTime or 0
    if (game and game.GameProcessPaused) or now>=save_due or now<save_due-SAVE_DELAY then
        local key=table.remove(save_order,1)
        if key then write_save(key) end
        if #save_order==0 then save_due=nil end
    end
end

-- Loot feedback ------------------------------------------------------------------
-- The "+5 gold" feedback of a hauled crate (four icons and texts) was built in
-- the pickup's own frame, on top of the rewards; build it one frame later.
local loot_feedback={}
local loot_selection
local function adapt_loot_layer(def)
    local selection=def.OnLootingSelection
    if type(selection)~="function" then return end
    loot_selection=selection
    def.OnLootingSelection=function(o,...)
        if o.IsEnabled then loot_feedback[#loot_feedback+1]={o,n=select("#",...),...} end
    end
    local deactivate=def.AfterDeactivate
    def.AfterDeactivate=function(o,...)
        for i=#loot_feedback,1,-1 do if loot_feedback[i][1]==o then table.remove(loot_feedback,i) end end
        if deactivate then return deactivate(o,...) end
    end
end
local function loot_frame()
    local job=table.remove(loot_feedback,1)
    if job and not job[1].IsDestroyed and job[1].IsEnabled then
        loot_selection(job[1],unpack(job,2,job.n+1))
    end
end

-- Battle controls -----------------------------------------------------------------
-- The chain-shot, spider-shot and dodge controls are built at the first battle
-- of each scene (their classes compiled on first use): most of that battle's
-- opening stall. Build them as the scene's loading screen closes, still at
-- the loading clock; the game's own guards then skip them at that battle.
function ACPVitaPrewarmBattle()
    local game=GetSubProcess and GetSubProcess("TagGameProcess")
    local controller=game and game.Controller
    local list=controller and controller.ControllerModeList and controller.ControllerModeList.Battle
    if type(list)~="table" or not game:GetEntity("TagBattleGUILayer") then return end
    for _,c in ipairs(list) do
        if type(c)=="table" and not c.IsDestroyed then
            if type(c.CreateQTEGUI)=="function" and not c.QTEGUICreated then c:CreateQTEGUI() end
            if c.ClassName=="ChainShotDefenseController" and not c.ChainShotButton then c:CreateGUIElements() end
            if c.ClassName=="DodgeDefenseController" and not (c.MoveLeftButton and c.MoveRightButton) then
                -- As DodgeDefenseController:OnStart does right after creating them.
                c:CreateGUIElements()
                for _,button in ipairs{c.MoveLeftButton,c.MoveRightButton} do
                    button:SetVisible(false);button.IsEnabled=false;button.ReadyTimer=false
                end
            end
        end
    end
end

-- Map-edge collisions ----------------------------------------------------------------
-- Pushed against the scene's edge, SceneLimitationsRectangle alternates its
-- collision and warning states every frame (its own TODO asks for a
-- hysteresis), and each collision spawned a splash effect and a new copy of
-- the collision sound: ~30 of each per second. Keep the collision itself;
-- play its splash and sound at most every 0.5 s.
local splash_at=setmetatable({},{__mode="k"})
local function adapt_player_ship(def)
    local collide=def.StartIslandCollision
    if type(collide)~="function" then return end
    def.StartIslandCollision=function(o,...)
        local now,last=AbsoluteTime or 0,splash_at[o]
        if last and now>=last and now-last<0.5 then
            local create,own_create=o.CreateEntity,rawget(o,"CreateEntity")
            rawset(o,"CreateEntity",function(self,class,init,...)
                if class=="JSVParticleFX" and type(init)=="table" and init.Type=="IslandCollisionFX" then return nil end
                return create(self,class,init,...)
            end)
            local sound=MainProcess and MainProcess:GetEntity("TagGameSound")
            local event=sound and sound.CallEventFiltered
            local own_event=sound and rawget(sound,"CallEventFiltered")
            if event then
                rawset(sound,"CallEventFiltered",function(self,name,...)
                    if name=="Sound_Player_Collision" then return nil end
                    return event(self,name,...)
                end)
            end
            local a,b,c=collide(o,...)
            rawset(o,"CreateEntity",own_create)
            if event then rawset(sound,"CallEventFiltered",own_event) end
            return a,b,c
        end
        splash_at[o]=now
        return collide(o,...)
    end
end

function ACPVitaHitchesFrame()
    saves_frame()
    loot_frame()
end

function ACPVitaHitches(def)
    local name=def.ClassName
    if name=="VariableTreeLoader" then adapt_variables(def)
    elseif name=="RealTimeVariableUpdater" then guarded(def,"LoadTable");guarded(def,"UpdateParametersWithoutEntity")
    elseif name=="DialogManager" then adapt_dialogs(def)
    elseif name=="FreeRoamingGUILayer" then adapt_loot_layer(def)
    elseif name=="PlayerShip" then adapt_player_ship(def) end
end
end hitches_module() end
