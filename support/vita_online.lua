-- PS Vita trophies and online leaderboards (native/online.c, trophies.c).
-- The game's own achievement updates unlock the matching trophies; they need
-- the NoTrpDrm plugin, and without it the start menu says so once. Bounty,
-- player level and the best Mayan-stone run go to the leaderboard server
-- (configured separately); the pause menu's leaderboard button shows them
-- in the game's own popup.
--   L/R, D-pad LEFT/RIGHT  previous/next board
--   CROSS, CIRCLE          close
-- Own function scope: the support files share one chunk and its 200 locals.
do local function online_module()
local band=bit.band
local RIGHT,LEFT,L,R=32,128,256,512
-- Trophy ids 1..99 (support/trophies/trophies.tsv); 0 is the platinum.
local TROPHIES={"pirate_wannabe","pirate_rookie","pirate_captain","pirate_expert","pirate_master",
    "pirate_legend","code_breaker","the_chaser","the_pursuer","a_legends_farewell","dangerous_prize",
    "freedom_fighter","friends_foes","sea_urchin","sea_rover","sea_wolf","sea_dragon","the_navigator",
    "the_explorer","the_adventurer","the_discoverer","the_seeker","the_pioneer","the_vanguard",
    "the_venturer","the_swashbuckler","the_daredevil","voyager","iceman","searcher","the_fortune_hunter",
    "master_of_the_seas","terror_of_the_seas","captain_blood","bully","mugger","brigand","destroyer",
    "plague_of_the_navy","greedy_pirate","able_pirate","pirate_king","ghost_ship","sail_like_the_wind",
    "delivery_boy","message_in_a_bottle","do_you_want_to_know_more","big_brother","abandon_ship",
    "jump_ship","ship_off","when_my_ship_comes_in","shape_up_or_ship_out","tight_ship","shipshape",
    "hell_cruiser","sword_of_the_ocean","sailing_in_style","the_fleet_builder","master_of_the_fleet",
    "join_me","the_family_grows","the_patriarch","bounty_call","bounty_lover","bounty_hugger",
    "bounty_killer","buccaneer","raider","brother_of_the_coast","pot_of_gold","gold_digger",
    "the_golden_boy","sunken_princess","slaver_jager","triumph","dutch_buster","amazon_hunt",
    "hms_nightmare","royal_enemy","no_mercy","fratricidal","the_king_executioner","amateur_fisherman",
    "professional_fisherman","fishaholic","bait_collector","captain_hook","too_many_fish_in_the_sea",
    "loose_lips_sink_ships","ships_that_pass_in_the_night","fire_ship","ship_line","daily_bread",
    "daily_grind","completionist","conscientious_pirate","dedicated_pirate","pirate_run"}
local trophy_ids={}
for i,name in ipairs(TROPHIES) do trophy_ids[name]=i end
local BOARDS={{id="bounty",title="BOUNTY"},{id="level",title="PLAYER LEVEL"},{id="stones",title="MAYAN STONES - BEST RUN"}}
local ROWS=8
-- Horizontal shift per unit of height of the slanted CONFIRMATION_POPUP_BIG
-- frame, in Content units (which scale with the popup's width).
local function slant(size) return 0.17*0.82/size end
local SLANT=slant(0.82)
-- Short lines placed one by one, so each stays inside the slanted frame.
-- The tai config that already loads kubridge: a second config.txt elsewhere
-- would make taiHEN ignore the first one.
local NOTICE={"Trophies need the NoTrpDrm plugin.","Copy NoTrpDrm.suprx to ur0:tai/ and",
    "add it under *main in the config.txt","that loads kubridge, then restart."}
local NOTICE_FIRMWARE="Firmware 3.60 to 3.68 only."

local api
local unlocked={}
local state={board=1,popup=false,notice=false,noticed=false,synced=false,previous=0,ready=0}

local function log(text) if ACPVitaLog then ACPVitaLog(text) end end
local function live(e) return type(e)=="table" and not e.IsDestroyed end

---------------------------------------------------------------- trophies
local function completed(a)
    if a.IsCompleted then return true end
    local count,max=tonumber(a.ProgressCount),tonumber(a.ProgressMax)
    return count~=nil and max~=nil and max>0 and count>=max
end
local function unlock(a)
    if type(a)~="table" or not ACPVitaTrophyUnlock then return end
    local id=trophy_ids[tostring(a.Name):match("([%w_]+)$")]
    if id and not unlocked[id] and completed(a) then
        unlocked[id]=true
        ACPVitaTrophyUnlock(id)
    end
end

---------------------------------------------------------------- leaderboards
local function submit(board,value)
    value=tonumber(value)
    if ACPVitaLeaderboardSubmit and value and value>0 then ACPVitaLeaderboardSubmit(board,math.floor(value)) end
end
local function profile() return MainProcess:GetEntity("TagPlayerProfile") end
-- Once per session in game: the saved bounty and level (which no event
-- reports until they change) and every achievement completed earlier.
local function sync()
    if state.synced then return end
    local game=GetSubProcess and GetSubProcess("TagGameProcess")
    local p=profile()
    if not live(game) or not live(p) then return end
    state.synced=true
    submit("bounty",p.ReputationPoints);submit("level",p.Level)
    local manager=MainProcess:GetEntity("TagAchievementsManager")
    local count=0
    if live(manager) and type(manager.Achievements)=="table" then
        for _,a in pairs(manager.Achievements) do
            if type(a)=="table" and completed(a) then count=count+1;unlock(a) end
        end
    end
    log("Online: synced bounty "..tostring(p.ReputationPoints)..", level "..tostring(p.Level)..", "..count.." achievements")
end

local function thousands(n)
    local s=tostring(math.floor(n))
    while true do
        local t,k=s:gsub("^(-?%d+)(%d%d%d)","%1,%2")
        s=t;if k==0 then return s end
    end
end
local function score(board,value)
    value=tonumber(value) or 0
    if board=="level" then return "Level "..value end
    if board=="stones" then return value==1 and "1 stone" or thousands(value).." stones" end
    return thousands(value)
end
-- "ACPLB 1 <board> <total>", "<rank>\t<value>\t<name>"..., "YOU\t<rank>\t<value>"
local function parse(text)
    local result={rows={},total=0}
    for line in tostring(text):gmatch("[^\n]+") do
        local total=line:match("^ACPLB 1 %S+ (%d+)")
        if total then result.total=tonumber(total)
        else
            local rank,value=line:match("^YOU\t(%d+)\t(%d+)")
            if rank then result.you={rank=tonumber(rank),value=tonumber(value)}
            else
                local r,v,name=line:match("^(%d+)\t(%d+)\t(.*)$")
                if r then result.rows[#result.rows+1]={rank=tonumber(r),value=tonumber(v),name=name} end
            end
        end
    end
    return result
end

local function label(parent,text,anchor,pivot,size,color)
    local e=parent:CreateEntity("LocalizedText2D",{
        Text=text,IsAlreadyLocalized=true,FontSize=size,Color=color or Color_White,LocalDepth=-1,
        AnchorPosition2D=anchor,PivotPosition2D=pivot,
        ScaleFactor2D=Vector2_UnitScale*MainProcess:GetEntity("TagGUIManager").ScaleDeviceFactor,
    })
    e._VitaText=text
    return e
end
-- The game's popup divider line.
local function divider(parent,anchor)
    return parent:CreateEntity("PakGUIElement2D",{Texture="POPUP_BOX_DEVIDER",AnchorPosition2D=anchor,
        PivotPosition2D=Vector2(0,0),ScaleFactor2D=Vector2_UnitScale*0.5,LocalDepth=-1})
end
local function set_text(e,text,color)
    if not live(e) then return end
    if e._VitaText~=text then e._VitaText=text;e.Text=text;e:UpdateTextAndFont(text) end
    if color then e.Color=color end
end

local function gold() return Color(1,0.82,0.38,1) end
local function grey() return Color(0.62,0.62,0.62,1) end
local function show_board(o)
    local board=BOARDS[state.board]
    set_text(o._VitaBoard,board.title)
    local status,text=-4,""
    if ACPVitaLeaderboardResult then status,text=ACPVitaLeaderboardResult(board.id) end
    o._VitaStatus=status
    local result=status==2 and parse(text) or {rows={}}
    local me=ACPVitaUserName and ACPVitaUserName() or ""
    for i=1,ROWS do
        local row=result.rows[i];local cells=o._VitaRows[i]
        local mine=row and result.you and row.rank==result.you.rank and row.name==me
        local color=mine and gold() or Color_White
        set_text(cells[1],row and ("#"..row.rank) or "",color)
        set_text(cells[2],row and row.name or "",color)
        set_text(cells[3],row and score(board.id,row.value) or "",color)
    end
    local message=""
    if status==1 or status==0 then message="Loading..."
    elseif status==-1 then message="Not connected. Connect to Wi-Fi to see the leaderboards."
    elseif status==-2 then message="The leaderboard server could not be reached."
    elseif status==-3 or status==-4 then message="The leaderboard is unavailable right now."
    elseif #result.rows==0 then message="No scores yet. Be the first!" end
    set_text(o._VitaMessage,message,grey())
    local you=""
    if status==2 then
        if result.you then you="You: #"..result.you.rank.." of "..thousands(result.total).."  -  "..score(board.id,result.you.value)
        else you="You are not on this board yet." end
    end
    set_text(o._VitaYou,you,gold())
end
local function fetch(o)
    local p=profile()
    if live(p) then submit("bounty",p.ReputationPoints);submit("level",p.Level) end
    if ACPVitaLeaderboardFetch then ACPVitaLeaderboardFetch(BOARDS[state.board].id) end
    show_board(o)
end

local function close(popup)
    if live(popup) and popup.State~="Hiding" and popup.State~="Hidden" then popup:HidePopup() end
end
-- The game's CommonPopup (title, background, animated buttons, back button)
-- with port-owned text rows. The popup keeps its root and Content hidden
-- (only their children draw): the buttons are navigated from the Content.
local function create_popup(parent,options)
    local popup=parent:CreateEntity("CommonPopup",options)
    local content=popup.Content
    content._VitaNavTransparentContainer=content
    content._VitaNavEntries={popup.OkButton}
    if popup.CancelButton then table.insert(content._VitaNavEntries,popup.CancelButton) end
    popup._PopupButtonBindedToDeviceBackButton=popup.OkButton
    return popup
end
local function open_leaderboard()
    if live(state.popup) then return end
    local pause=MainProcess:GetEntity("TagPauseGUILayer")
    if not live(pause) or not (pause.IsScrolledDown or pause.ScrollingDown) then
        log("Leaderboard: no pause menu to show it on");return
    end
    local popup=create_popup(pause,{
        Type="BIG",SizePopup=0.82,Depth=-20,DestroyOnHide=true,Priority=1,
        OkButtonText="GAME_00024",OkBtonAnchor=Vector2(0,-1),
        OkButtonFunction=function(button) close(button.Parent.Parent) end,
    })
    local content=popup.Content
    -- The frame is a parallelogram: centred lines follow its slant, the
    -- table's columns stay straight inside its narrowest part.
    local function at(x,y) return Vector2(x+SLANT*y,y) end
    label(content,"LEADERBOARDS",at(0,1),Vector2(0,1),40)
    popup._VitaBoard=label(content,"",at(0,0.66),Vector2(0,0),28,gold())
    api.icon(content,"_VitaPrevious","L",at(-0.78,0.66),0.6)
    api.icon(content,"_VitaNext","R",at(0.78,0.66),0.6)
    divider(content,at(0,0.55))
    popup._VitaRows={}
    for i=1,ROWS do
        local y=0.42-(i-1)*0.125
        popup._VitaRows[i]={
            label(content,"",Vector2(-0.6,y),Vector2(1,0),24),
            label(content,"",Vector2(-0.55,y),Vector2(-1,0),24),
            label(content,"",Vector2(0.7,y),Vector2(1,0),24),
        }
    end
    divider(content,at(0,-0.58))
    popup._VitaMessage=label(content,"",Vector2(0,0),Vector2(0,0),26,grey())
    popup._VitaYou=label(content,"",at(0,-0.69),Vector2(0,0),24,gold())
    state.popup=popup
    popup:ShowPopup()
    fetch(popup)
    log("Leaderboard: opened "..BOARDS[state.board].id)
end

local function open_notice(parent)
    state.noticed=true
    local popup=create_popup(parent,{
        Type="BIG",SizePopup=0.78,Depth=-20,DestroyOnHide=true,Priority=1,
        OkButtonText="GAME_00017",CancelButtonText="GAME_00012",
        -- Apart, and centred on the frame's bottom edge (left of the middle).
        OkBtonAnchor=Vector2(0.36,-1),CancelBtonAnchor=Vector2(-0.8,-1),
        OkButtonFunction=function(button) close(button.Parent.Parent) end,
        CancelButtonFunction=function(button)
            api.setting(5,1);log("Trophy notice: do not show again")
            close(button.Parent.Parent)
        end,
    })
    local content=popup.Content
    local shift=slant(0.78)
    label(content,"TROPHIES UNAVAILABLE",Vector2(shift,1),Vector2(0,1),44)
    for i,line in ipairs(NOTICE) do
        local y=0.5-(i-1)*0.17
        label(content,line,Vector2(shift*y,y),Vector2(0,0),28)
    end
    label(content,NOTICE_FIRMWARE,Vector2(shift*-0.26,-0.26),Vector2(0,0),24,grey())
    set_text(popup.CancelButton.Text,"DON'T SHOW")
    state.notice=popup
    popup:ShowPopup()
    log("Trophy notice shown")
end
-- Start menu: once per launch, when trophies are known to be unavailable.
local function main_menu_ready(o,ready)
    if not ready then state.ready=0;return end
    state.ready=state.ready+math.max(0,Dt or 0)
    if state.noticed or state.ready<1.5 or not ACPVitaTrophyState then return end
    local trophies=ACPVitaTrophyState()
    if trophies==-1 and api.setting(5)==0 then open_notice(o.PageLayer)
    elseif trophies~=0 then state.noticed=true end
end

local function popup_frame(popup)
    if not live(popup) or popup.State=="Hidden" then return false end
    local opened=popup.State=="Opened"
    local b=api.pad()
    if popup==state.popup and opened then
        local step=0
        local function pressed(mask) return band(b,mask)~=0 and band(state.previous,mask)==0 end
        if pressed(L) or pressed(LEFT) then step=-1 elseif pressed(R) or pressed(RIGHT) then step=1 end
        if step~=0 then
            state.board=(state.board-1+step)%#BOARDS+1
            api.consume(band(b,L+R+LEFT+RIGHT));fetch(popup)
        elseif popup._VitaStatus==1 or popup._VitaStatus==0 then show_board(popup) end
    end
    state.previous=b
    api.navigation(popup.Content,opened,true)
    return true
end

function ACPVitaOnlineFrame()
    sync()
    if not popup_frame(state.popup) then state.popup=false end
    if not popup_frame(state.notice) then state.notice=false end
    if not state.popup and not state.notice then state.previous=api and api.pad() or 0 end
end

function ACPVitaOnline(def,online_api)
    api=online_api
    local name=def.ClassName
    if name=="SocialManager" and def.OnBountyChanged then
        api.after(def,"OnBountyChanged",function(o,bounty) submit("bounty",bounty) end)
        api.after(def,"OnPlayerLevelSend",function(o,level) submit("level",level) end)
        api.after(def,"UpdateAchievement",function(o,achievement) unlock(achievement) end)
        -- The pause menu's leaderboard button; the options' silent
        -- reconnection (hidden) keeps the original no-op path.
        local show=def.OnShowGameCenter
        def.OnShowGameCenter=function(o,hidden,...)
            if hidden then return show(o,hidden,...) end
            open_leaderboard()
        end
        def.OnShowLeaderboard=function() open_leaderboard() end
    elseif name=="PlayerProfile" and def.EndRunner then
        api.after(def,"EndRunner",function(o,stones,current,packs,treasure,success)
            if success then submit("stones",stones) end
        end)
    elseif name=="MainMenu" then
        api.after(def,"OnUpdate",function(o)
            local layer=o.PageLayer
            local button=not o.IntroVideoLayer and live(layer) and layer.ButtonPlay
            local manager=MainProcess:GetEntity("TagDeviceBackButtonManager")
            main_menu_ready(o,button and button.Visible and button.IsEnabled and not manager:HasDisplayedPopup()
                and not manager._GUILayer and not MainProcess:GetEntity("TagFadeFX").Visible)
        end)
    end
end
end online_module() end
