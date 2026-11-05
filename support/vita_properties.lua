-- Same dispatch as the pinned ARM ClassSystem factories (0x52a314/0x52a328).
-- Lua closures let LuaJIT record table access and Lua getters across __index.
-- Values are never cached: live class/getter/setter table edits stay visible.
local rawget,rawset=rawget,rawset
local function index_factory(class,getters)
    return function(object,key)
        local getter=rawget(getters,key)
        if getter~=nil then return getter(object) end
        return rawget(class,key)
    end
end
local function newindex_factory(class,setters)
    return function(object,key,value)
        local setter=rawget(setters,key)
        if setter~=nil then setter(object,value)
        else rawset(object,key,value) end
    end
end
rawset(_G,"ACPVitaOptimizeProperties",function()
    local old_index=rawget(_G,"ClassSystem__indexFactory")
    local old_newindex=rawget(_G,"ClassSystem__newindexFactory")
    if type(old_index)~="function" or type(old_newindex)~="function" then return false end
    local benchmark=rawget(_G,"ACPVitaPropertyBenchmark")
    if benchmark then benchmark(old_index,old_newindex,index_factory,newindex_factory) end
    -- The default ~1000-trace ceiling repeatedly evicts compiled game loops.
    -- Keep 4096 traces, with at most 2 MiB code per VM inside the existing
    -- shared 4 MiB executable pool. Trace metadata remains in the 96 MiB heap.
    -- Explicit game-requested flushes and LuaJIT's OOM fallback still apply.
    require('jit.opt').start('maxtrace=4096','maxmcode=2048')
    rawset(_G,"ClassSystem__indexFactory",index_factory)
    rawset(_G,"ClassSystem__newindexFactory",newindex_factory)
    return true
end)
