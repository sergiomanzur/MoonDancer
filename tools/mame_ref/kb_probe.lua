local frame=0
local function log(s) emu.print_info(s) end
local function svc() return manager.machine.ioport.ports[":SERVICE"] end
emu.register_frame_done(function()
 frame=frame+1
 if frame==900 then
   local kb=manager.machine.natural_keyboard
   log("kb="..tostring(kb))
   local ok,err=pcall(function() kb:post_keypress("5") end)
   log("post_keypress ok="..tostring(ok).." err="..tostring(err))
   local ok2,err2=pcall(function() kb:post_char("5") end)
   log("post_char ok="..tostring(ok2).." err="..tostring(err2))
 end
 if frame>=900 and frame<960 and frame%5==0 then
   local ok,rv=pcall(function() return svc():read() end)
   log(string.format("frame %d portread=%s",frame,tostring(rv)))
 end
 if frame%60==0 and frame>=900 and frame<=1400 then
   local sh=manager.machine.memory.shares[":workram"]
   log(string.format("frame %d credits %02X %02X %02X",frame,sh:read_u8(0x2000),sh:read_u8(0x2002),sh:read_u8(0x2004)))
 end
end,"frame")
