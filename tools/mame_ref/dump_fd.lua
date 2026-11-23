local n=0
local function log(s) emu.print_info("PROBE "..s) end
emu.register_frame_done(function()
 n=n+1
 if n<=4 then
   local sh=manager.machine.memory.shares[":workram"]
   local f=io.open("build/mame_fd"..n..".bin","wb")
   if f then for i=0,sh.size-1 do f:write(string.char(sh:read_u8(i))) end f:close() end
   log("dumped fd "..n)
 end
end,"frame")
