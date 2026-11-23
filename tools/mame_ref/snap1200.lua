local frame=0
local function log(s) emu.print_info(s) end
local cpu=manager.machine.devices[":maincpu"]
local space=cpu.spaces["program"]
local rtap=space:install_read_tap(0xE40000,0xE4003F,"io",function(offset,data,mask)
  if offset==0xE40008 then
    if frame>=900 and frame<940 then data=data & ~0x01 end
    if frame>=1000 and frame<1040 then data=data & ~0x10 end
  end
  return data,mask
end)
emu.register_frame_done(function()
 frame=frame+1
 rtap:reinstall()
 if frame==1200 then
   manager.machine.video:snapshot()
   local sh=manager.machine.memory.shares[":workram"]
   log(string.format("snap frame 1200 E036=%02X E000=%02X",sh:read_u8(0x2036),sh:read_u8(0x2000)))
 end
end,"frame")
