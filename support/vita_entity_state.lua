-- The pinned release's Lua reads IsDestroyed only as a truth value and writes
-- true after destruction. Keep the live state directly on each entity so
-- frequent checks do not enter the C ClassSystem callback for an absent field.
-- The engine's native collision/trigger reports tested it against nil; they
-- read it as a truth value too since native/destroyed_checks.c.
-- OnUpdate is a fixed class callback after construction. Store its existing
-- function reference, preserving per-instance overrides and callback order.
local rawget,rawset,type=rawget,rawset,type
local prepared=setmetatable({},{__mode='k'})
local create_wrapper,reload_wrapper
local disabled=false
local function pack(...)return {n=select('#',...),...}end
local function plain(map,key)return not map or rawget(map,key)==nil end
local function prepare(o)
    if disabled or ClassSystemDebug or type(o)~='table' or prepared[o]~=nil then return o end
    if rawget(o,'IsDestroyed') then return o end
    local class=o.__Class
    if type(class)~='table' or getmetatable(o)~=rawget(class,'__Metatable') or
       type(rawget(class,'Destroy'))~='function' then return o end
    local getters,setters=rawget(class,'__GetterMap'),rawget(class,'__SetterMap')
    if not plain(getters,'IsDestroyed') or not plain(setters,'IsDestroyed') or
       rawget(class,'IsDestroyed')~=nil or rawget(o,'IsDestroyed')~=nil then return o end
    local update=false
    if plain(getters,'OnUpdate') and plain(setters,'OnUpdate') and rawget(o,'OnUpdate')==nil then
        local value=rawget(class,'OnUpdate')
        if type(value)=='function' then update=value;rawset(o,'OnUpdate',value) end
    end
    prepared[o]=update
    rawset(o,'IsDestroyed',false)
    return o
end
function ACPVitaEntityStateInstall()
    if disabled then return end
    local create=rawget(_G,'CreateInstance')
    if type(create)~='function' or create==create_wrapper then return end
    create_wrapper=function(...)return prepare(create(...))end
    CreateInstance=create_wrapper
    local reload=rawget(_G,'ClassSystem_HotReload')
    if type(reload)=='function' and reload~=reload_wrapper then
        reload_wrapper=function(...)
            -- Normal module loading also recompiles classes with no argument.
            -- Restore inheritance while it runs, then prepare against the new
            -- class maps. Explicit developer remapping keeps this adapter off.
            disabled=true
            local previous=prepared
            prepared=setmetatable({},{__mode='k'})
            for o,update in pairs(previous) do
                if rawget(o,'IsDestroyed')==false then
                    rawset(o,'IsDestroyed',nil)
                    if update and rawget(o,'OnUpdate')==update then rawset(o,'OnUpdate',nil) end
                end
            end
            if select(1,...)~=nil then return reload(...) end
            local result=pack(pcall(reload,...))
            if not result[1] then error(result[2],0) end
            disabled=false
            for o in pairs(previous) do prepare(o) end
            return unpack(result,2,result.n)
        end
        ClassSystem_HotReload=reload_wrapper
    end
end
