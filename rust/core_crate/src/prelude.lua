-- pico386 compiler prelude: higher-order builtins implemented in Lua.
-- Prepended to every cart source before parsing (see lib.rs). These
-- functions compile to SETGLOBAL into the reserved builtin slots
-- (P386_BUILTIN_ALL / P386_BUILTIN_FOREACH / P386_BUILTIN_IPAIRS), which
-- have no CFUNC registered on the C side.
function all(t)
 if (t==nil) return function() end
 local i=1
 local prev=nil
 return function()
  if t[i]==prev then i+=1 end
  while t[i]==nil and i<=#t do i+=1 end
  prev=t[i]
  return t[i]
 end
end
function foreach(t,f)
 for v in all(t) do f(v) end
end
function ipairs(t)
 local i=0
 return function()
  i+=1
  if (t[i]~=nil) return i,t[i]
 end
end
