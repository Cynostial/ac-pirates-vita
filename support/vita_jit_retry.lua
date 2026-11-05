-- Stop repeatedly recording roots which have never produced a useful trace
-- and consistently fail at an unsupported C call. Interpreter behavior, nested
-- functions, successful traces and the executable/data limits are unchanged.
-- The policy is installed only by the private experiment until measured.
function ACPVitaJitRetryCreate(engine,util,notify)
    local functions=setmetatable({},{__mode='k'})
    local groups=setmetatable({},{__mode='v'})
    local pending=setmetatable({},{__mode='k'})
    local roots={}
    local tracked,disabled,failures=0,0,0
    local function record(fn)
        if type(fn)~='function' then return nil end
        local entry=functions[fn]
        if entry then return entry end
        if tracked>=2048 then return nil end
        local info=util.funcinfo(fn)
        if not info.bytecodes or tostring(info.source):match('^@vita%-') then return nil end
        -- This VM does not expose prototype addresses. Shared-prototype
        -- closures have identical debug metadata, so conservatively group by
        -- it. A collision only preserves additional JIT code: any successful
        -- member protects the whole group, and only the queued function is
        -- passed to jit.off. No address/ABI assumptions or value caching.
        local key=table.concat({tostring(info.source),tostring(info.linedefined),
            tostring(info.lastlinedefined),tostring(info.bytecodes),
            tostring(info.params),tostring(info.upvalues)},'|')
        entry=groups[key]
        if not entry then
            entry={failures=0,source=tostring(info.source),line=info.linedefined}
            groups[key]=entry;tracked=tracked+1
        end
        functions[fn]=entry
        return entry
    end
    local function event(kind,tr,fn,pc,reason)
        if kind=='start' then roots[tr]=fn
        elseif kind=='flush' then roots={}
        elseif kind=='stop' or kind=='abort' then
            local root=roots[tr];roots[tr]=nil
            local entry=record(root)
            if not entry then return end
            if kind=='stop' then entry.compiled=true
            elseif reason==13 and not entry.compiled and not entry.disabled then
                -- Pinned LuaJIT trace error 13: NYI C function. Other reasons
                -- (changing types, loop limits, memory pressure) are untouched.
                failures=failures+1;entry.failures=entry.failures+1
                if entry.failures>=8 then pending[root]=entry end
            end
        end
    end
    local function step()
        for fn,entry in pairs(pending) do
            pending[fn]=nil
            if disabled<128 and not entry.compiled and not entry.disabled then
                -- Run at the next frame boundary, outside the trace callback.
                -- No recursive flag: child functions retain their JIT policy.
                engine.off(fn)
                entry.disabled=true;disabled=disabled+1
                if notify then notify(entry.source,entry.line,entry.failures) end
            end
        end
    end
    local function stats()return tracked,disabled,failures end
    engine.off(event,true);engine.off(step,true);engine.off(record,true)
    return event,step,stats
end
