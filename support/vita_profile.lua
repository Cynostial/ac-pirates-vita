-- Attribute complete composed callbacks to the entity
-- actually scheduled. Unsampled frames execute the original engine functions.
-- This avoids wrapping every inherited method on every frame and obscuring
-- callback composition overhead inside PriorityProcess's apparent self time.
local adapt,phase=ACPVitaAdapt,ACPVitaMethodPhase
local bounded=rawget(_G,'ACPVitaBoundedSamples')==1
local ids={}
local jit_report=function()end
local jit_step=function()end
do
    local engine=rawget(_G,'jit')
    if rawget(_G,'ACPVitaTraceEvents')==1 and engine and engine.attach then
        local util=require('jit.util')
        local events,aborts={},{}
        local labels=setmetatable({},{__mode='k'})
        local retry_event,retry_stats
        if rawget(_G,'ACPVitaJitRetryEnabled')==1 then
            retry_event,jit_step,retry_stats=ACPVitaJitRetryCreate(engine,util,function(source,line,n)
                ACPVitaLog('JIT retry stopped: '..source..':'..tostring(line)..' failures='..n)
            end)
        end
        local function trace_event(event,tr,func,pc,reason)
            if retry_event then retry_event(event,tr,func,pc,reason) end
            events[event]=(events[event] or 0)+1
            if event=='abort' then
                local label=type(func)=='function' and labels[func]
                if not label then
                    local info=type(func)=='function' and util.funcinfo(func) or {}
                    label=tostring(info.source)..':'..tostring(info.linedefined)
                    if type(func)=='function' then labels[func]=label end
                end
                local key=tostring(reason)..' '..label..' pc='..tostring(pc)
                if aborts[key] or (events.abort or 0)<512 then aborts[key]=(aborts[key] or 0)+1 end
            end
        end
        engine.off(trace_event,true)
        engine.attach(trace_event,'trace')
        jit_report=function()
            if ACPVitaObjectWorkStats then local events,leaves=ACPVitaObjectWorkStats();ACPVitaLog('Compiled update events: '..events..' leaves='..leaves) end
            ACPVitaLog('JIT events: start='..(events.start or 0)..' stop='..(events.stop or 0)..' abort='..(events.abort or 0)..' flush='..(events.flush or 0))
            if retry_stats then
                local tracked,disabled,failed=retry_stats()
                ACPVitaLog('JIT retry policy: tracked='..tracked..' disabled='..disabled..' unsupported-aborts='..failed)
            end
            local sorted={}
            for key,n in pairs(aborts) do sorted[#sorted+1]={key,n} end
            table.sort(sorted,function(a,b)return a[2]>b[2]end)
            for i=1,math.min(8,#sorted) do ACPVitaLog('JIT abort: n='..sorted[i][2]..' '..sorted[i][1]) end
            events,aborts={},{}
        end
        engine.off(jit_report,true)
    end
end
local root_frames=0
local function id(name)
    local value=ids[name]
    if value==nil then value=ACPVitaMethodRegister(name);ids[name]=value end
    return value
end
local function callback(object,key)
    local label=id('entity:'..tostring(object.ClassName)..':'..key)
    local active=phase(label,1)==1
    object[key](object)
    if active then phase(label,0) end
end
local function process_update(original,label)
    return function(o)
        if bounded and not ACPVitaSampleActive then return original(o) end
        if o==rawget(_G,'MainProcess') then
            jit_step()
            root_frames=root_frames+1
            if root_frames%240==0 then jit_report() end
        end
        if phase(label,1)~=1 then return original(o) end
        if o._ReorderUpdateList then
            local count=original(o);phase(label,0);return count
        end
        if not o._Pause then
            local list=o._UpdateList
            local i=1
            local entity=list[i]
            while entity do
                if entity.IsDestroyed then ListRemove(list,i)
                else
                    if entity.OnUpdate then callback(entity,'OnUpdate') end
                    i=i+1
                end
                entity=list[i]
            end
        end
        local count=#o._UpdateList
        for _,child in ipairs(o._SubProcessList) do count=count+child:Update() end
        phase(label,0);return count
    end
end
local function loop_update(original,label)
    return function(o)
        if bounded and not ACPVitaSampleActive then return original(o) end
        if phase(label,1)~=1 then return original(o) end
        original(o)
        phase(label,0)
    end
end
function ACPVitaAdapt(original)
    return adapt(function(def)
        if def.ClassName=='PakProcess' or def.ClassName=='PriorityProcess' then
            def.Update=process_update(def.Update,id('dispatch:'..def.ClassName))
        elseif def.ClassName=='GameLoop' then
            def.Update=loop_update(def.Update,id('dispatch:GameLoop'))
        end
        return original(def)
    end)
end
