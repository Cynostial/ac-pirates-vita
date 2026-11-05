-- Private experiment: compile fixed-signature update events once, during
-- class construction. Preserve callback order and destruction checks. Only
-- the pinned ClassSystem's closures, with single-argument Lua leaves, qualify.
-- Unknown, native, variadic and camera-argument callbacks retain the original.
local util=require('jit.util')
local rawget,rawset=rawget,rawset
local compiled=setmetatable({},{__mode='k'})
local factories={}
local changed,leaves=0,0
local function specialize(fn,depth)
    if type(fn)~='function' or depth>16 then return nil end
    local cached=compiled[fn]
    if cached~=nil then return cached or nil end
    local info=util.funcinfo(fn)
    if not info.bytecodes then compiled[fn]=false;return nil end
    if not info.isvararg and info.params<=1 then return fn end
    if info.source~='@__ClassSystem/ClassSystem.lua' or info.params~=1 or
       (info.linedefined~=877 and info.linedefined~=891 and info.linedefined~=904) then
        compiled[fn]=false;return nil
    end
    local values={}
    for i=1,info.upvalues do
        local name,value=debug.getupvalue(fn,i)
        if not name then compiled[fn]=false;return nil end
        values[name]=value
    end
    local list,initial_check
    if info.linedefined==904 and type(values.delegateList)=='table' and values.rawget==rawget and info.upvalues==2 then
        list=values.delegateList;initial_check=true
    elseif info.linedefined~=904 and type(values.fctOne)=='function' and type(values.fctTwo)=='function' and
           (info.upvalues==2 or (info.upvalues==3 and type(values.fctTree)=='function')) then
        list={values.fctOne,values.fctTwo,values.fctTree};initial_check=false
    else compiled[fn]=false;return nil end
    local n=#list
    if n<2 or n>32 then compiled[fn]=false;return nil end
    local functions={}
    for i=1,n do
        functions[i]=specialize(rawget(list,i),depth+1)
        if not functions[i] then compiled[fn]=false;return nil end
    end
    -- Generated fixed calls avoid the unsupported VARG bytecode and repeated
    -- delegate-list iteration. Compilation occurs at load time, never per frame.
    local key=n..':'..tostring(initial_check)
    local factory=factories[key]
    if not factory then
        local locals,args,body={},{},{}
        for i=1,n do locals[i]='f'..i;args[i]='list['..i..']' end
        body[1]='return function(list) local '..table.concat(locals,',')..'='..table.concat(args,',')..';return function(o) '
        for i=1,n do
            if i>1 or initial_check then body[#body+1]='if o.IsDestroyed then return end;' end
            body[#body+1]='f'..i..'(o);'
        end
        body[#body+1]='end end'
        local chunk=loadstring(table.concat(body),'@vita-compiled-update')
        if not chunk then compiled[fn]=false;return nil end
        factory=chunk()
        factories[key]=factory
    end
    local result=factory(functions)
    compiled[fn]=result;changed=changed+1;leaves=leaves+n
    return result
end
function ACPVitaCompileUpdate(fn)return specialize(fn,0) or fn end
function ACPVitaObjectWorkStats()return changed,leaves end
function ACPVitaObjectWorkInstall(mode)
    local index,newindex=ClassSystem__indexFactory,ClassSystem__newindexFactory
    if type(index)~='function' or type(newindex)~='function' then return false end
    local function compile(class)
        if mode>=2 then
            local update=rawget(class,'OnUpdate')
            if update then rawset(class,'OnUpdate',ACPVitaCompileUpdate(update)) end
        end
    end
    ClassSystem__indexFactory=function(class,getters)
        compile(class)
        if mode==2 then return index(class,getters) end
        return function(object,key)
            local getter=rawget(getters,key)
            if getter~=nil then return getter(object) end
            return rawget(class,key)
        end
    end
    ClassSystem__newindexFactory=function(class,setters)
        compile(class)
        if mode==2 then return newindex(class,setters) end
        return function(object,key,value)
            local setter=rawget(setters,key)
            if setter~=nil then setter(object,value) else rawset(object,key,value) end
        end
    end
    -- No-getter/no-setter classes do not invoke either native factory.
    local create=CreateInstance
    if mode>=2 and type(create)=='function' then
        CreateInstance=function(class,namespace,initial)
            local object=create(class,namespace,initial)
            if object then compile(object.__Class) end
            return object
        end
    end
    return true
end
