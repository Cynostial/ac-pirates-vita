-- Stable render callback order with O(1) membership/removal. Removed entries
-- are tombstones until a frame boundary; no entities are retained by them.
-- The original render manager is the sole owner of this private array.
local rawget,rawset=rawget,rawset
local optimize=true -- ACP_RENDER_QUEUE_MODE
local stats
local function add(key,n) if stats then stats[key]=(stats[key] or 0)+(n or 1) end end
local frames=0
local function report()
    frames=frames+1
    if frames==120 then
        local keys={'registered','removed','scan_steps','shifted_slots','callbacks','text_callbacks','hidden_text_callbacks','hidden_readds','compactions','compaction_slots'}
        local values={}
        for _,key in ipairs(keys) do values[#values+1]=key..'='..(stats[key] or 0) end
        ACPVitaLog('Render work window: '..table.concat(values,' '))
        stats={};frames=0
    end
end
local function queue(o)
    local q=rawget(o,"_VitaRenderQueue")
    if not q then
        local list=o._RegisteredBeforeRenderList
        q={list=list,slots={},dead=0,walking=false}
        for i=1,#list do q.slots[list[i]]=i end
        rawset(o,"_VitaRenderQueue",q)
    end
    return q
end
local function register(o,m)
    local q=queue(o)
    if q.slots[m] then return end
    local i=#q.list+1
    q.list[i]=m;q.slots[m]=i
    if stats then add('registered') end
end
local function unregister(o,m)
    local q=queue(o);local i=q.slots[m]
    if not i then return end
    q.slots[m]=nil;q.list[i]=false;q.dead=q.dead+1
    if stats then add('removed') end
end
local function update(o)
    local q=queue(o);local list=q.list
    -- Compact before dispatch, never while callbacks are modifying the list.
    -- At most 31 or one quarter of the slots remain unused between frames.
    if not q.walking and q.dead>0 and (q.dead>=32 or q.dead*4>=#list) then
        local write=1;local size=#list
        for read=1,size do
            local m=list[read]
            if m then list[write]=m;q.slots[m]=write;write=write+1 end
        end
        for i=write,size do list[i]=nil end
        q.dead=0
        if stats then add('compactions');add('compaction_slots',size) end
    end
    -- Snapshot the upper bound. New registrations begin on the next pass.
    -- Removing an older slot does not shift and repeat the current callback.
    local walking=q.walking;q.walking=true
    for i=#list,1,-1 do
        local m=list[i]
        if m then
            if stats then add('callbacks') end
            m:BeforeRender()
        end
    end
    q.walking=walking
    if stats then report() end
end
local function visible(o,v)
    if not v and o._IsRegistered then
        o:Unregister();o._IsRegistered=false
    elseif v and not o._IsRegistered then
        o:Register();o._IsRegistered=true
    end
    o._Visible=v
    if o.TextMesh and o.__HasText then o.TextMesh.Visible=v end
    if o.Shadow and o.FontHasShadow then o.Shadow.Visible=v end
end
function ACPVitaRenderWork(def)
    if rawget(_G,'ACPVitaRenderWorkProfile') and not stats then stats={} end
    local name=def.ClassName
    if name=='RenderManagerBase' then
        if optimize then
            def.RegisterBeforeRender=register
            def.UnregisterBeforeRender=unregister
            def.OnUpdate=update
        elseif stats then
            -- Exact original loops with local work counters, not a timing test.
            def.RegisterBeforeRender=function(o,m)
                local list=o._RegisteredBeforeRenderList
                for i=#list,1,-1 do add('scan_steps');if m==list[i] then return end end
                table.insert(list,m);add('registered')
            end
            def.UnregisterBeforeRender=function(o,m)
                local list=o._RegisteredBeforeRenderList
                for i=#list,1,-1 do
                    add('scan_steps')
                    if m==list[i] then add('shifted_slots',#list-i);table.remove(list,i);add('removed');break end
                end
            end
            def.OnUpdate=function(o)
                for i=#o._RegisteredBeforeRenderList,1,-1 do
                    add('callbacks');o._RegisteredBeforeRenderList[i]:BeforeRender()
                end
                report()
            end
        end
        ACPVitaLog(optimize and 'Render work: stable sparse callback queue' or 'Render work: original callback queue (local comparison)')
    elseif name=='PakGeeaText' then
        local found=false
        for _,entry in ipairs(def) do
            if type(entry)=='table' and entry[1]=='Visible' and entry.Set then
                found=true
                if optimize then entry.Set=visible
                elseif stats then
                    local original=entry.Set
                    entry.Set=function(o,v)
                        local registered=o._IsRegistered
                        original(o,v)
                        if not v and not registered and o._IsRegistered then add('hidden_readds') end
                    end
                end
            end
        end
        assert(found,'Pinned PakGeeaText visibility property missing')
        if stats then
            local original=def.BeforeRender
            def.BeforeRender=function(o,...)
                add('text_callbacks');if not o._Visible then add('hidden_text_callbacks') end
                return original(o,...)
            end
        end
        ACPVitaLog(optimize and 'Render work: repeated hide keeps text unregistered' or 'Render work: original text visibility (local comparison)')
    end
end
