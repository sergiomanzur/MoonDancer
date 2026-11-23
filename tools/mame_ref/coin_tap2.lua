local frame=0
local function log(s) emu.print_info(s) end
local cpu=manager.machine.devices[":maincpu"]
local space=cpu.spaces["program"]
local tap=space:install_read_tap(0xE40000,0xE4003F,"io",function(offset,data,mask)
  if offset==0xE40008 then
    if frame>=900 and frame<940 then data=data & ~0x01 end
    if frame>=1000 and frame<1040 then data=data & ~0x10 end
  end
  return data,mask
end)
local wtap=space:install_write_tap(0xFFE000,0xFFE00F,"credits",function(offset,data,mask)
  log(string.format("CW %06X=%04X m=%04X PC=%06X frame=%d",offset,data,mask,cpu.state["CURPC"].value,frame))
end)
emu.register_frame_done(function()
 frame=frame+1
 tap:reinstall(); wtap:reinstall()
 if frame%30==0 and frame>=880 and frame<=1200 then
   local sh=manager.machine.memory.shares[":workram"]
   log(string.format("frame %d credits %02X %02X %02X",frame,sh:read_u8(0x2000),sh:read_u8(0x2002),sh:read_u8(0x2004)))
 end
end,"frame")
