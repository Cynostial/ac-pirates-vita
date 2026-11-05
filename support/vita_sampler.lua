-- Profile builds only (lua-sampler.flag): attribute wall time to Lua source
-- lines and functions with a VM count hook, to find where loading goes. The
-- time between two hooks includes native calls made from the sampled code.
-- JIT is switched off so the hook also fires in code that would be traced.
-- Besides 10 s windows, any single game frame (AbsoluteTime is constant
-- within one) whose sampled time reaches ACPVitaHitchMs is reported on its
-- own with its hottest functions, three-level call stacks and lines.
do
local clock=rawget(_G,'ACPVitaClock')
if clock and debug and debug.sethook then
    local getinfo=debug.getinfo
    local lines,funcs,calls={},{},{}
    local frame_funcs,frame_stacks,frame_lines={},{},{}
    local frame_time,frame_spent,frame_start,hitches=nil,0,0,0
    ACPVitaHitchMs=ACPVitaHitchMs or 60
    local last=clock()
    local window=last
    local function top(map,n)
        local list={}
        for k,v in pairs(map) do list[#list+1]={k,v} end
        table.sort(list,function(a,b)return a[2]>b[2] end)
        local out={}
        for i=1,math.min(n,#list) do out[#out+1]=string.format('%s=%dms',list[i][1],list[i][2]/1000) end
        return table.concat(out,' ')
    end
    local function dump(now)
        local spent=now-window
        ACPVitaLog(string.format('Lua sampler window %.1fs: functions %s',spent/1e6,top(funcs,12)))
        ACPVitaLog('Lua sampler lines: '..top(lines,12))
        ACPVitaLog('Lua sampler callers: '..top(calls,8))
        lines,funcs,calls={},{},{}
        window=now
    end
    local function where(info)
        return (info.name or '?')..'@'..info.short_src..':'..(info.linedefined or 0)
    end
    local function hook()
        local now=clock()
        local dt=now-last
        if dt<0 then dt=0 end
        last=now
        local t=rawget(_G,'AbsoluteTime')
        if t~=frame_time then
            if frame_spent>=ACPVitaHitchMs*1000 then
                hitches=hitches+1
                ACPVitaLog(string.format('Hitch %d: %d ms sampled, %d ms wall: %s',hitches,frame_spent/1000,(now-frame_start)/1000,top(frame_funcs,8)))
                ACPVitaLog('Hitch stacks: '..top(frame_stacks,8))
                ACPVitaLog('Hitch lines: '..top(frame_lines,10))
            end
            frame_time,frame_spent,frame_start=t,0,now
            frame_funcs,frame_stacks,frame_lines={},{},{}
            dt=0 -- the gap since the last hook was rendering, outside Lua
        end
        local info=getinfo(2,'Sln')
        if info then
            local src=info.short_src
            local line=src..':'..(info.currentline or 0)
            local func=src..':'..(info.linedefined or 0)
            lines[line]=(lines[line] or 0)+dt
            funcs[func]=(funcs[func] or 0)+dt
            local caller=getinfo(3,'Sln')
            if caller then
                local key=caller.short_src..':'..(caller.currentline or 0)
                calls[key]=(calls[key] or 0)+dt
            end
            frame_spent=frame_spent+dt
            local name=where(info)
            frame_funcs[name]=(frame_funcs[name] or 0)+dt
            frame_lines[line]=(frame_lines[line] or 0)+dt
            local outer=getinfo(4,'Sn')
            local stack=name..'<'..(caller and where(caller) or '-')..'<'..(outer and where(outer) or '-')
            frame_stacks[stack]=(frame_stacks[stack] or 0)+dt
        end
        if now-window>10000000 then dump(now) end
    end
    if jit and jit.off then jit.off() end
    debug.sethook(hook,'',200)
    ACPVitaLog('Lua sampler: count hook every 200 VM instructions, JIT off, 10 s windows, frames over ACPVitaHitchMs')
end
end
