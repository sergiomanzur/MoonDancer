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
 frame=frame+1; rtap:reinstall()
 if frame==1210 then
   local sh=manager.machine.memory.shares[":textram"]
   for _,a in ipairs({0xE80,0xE82,0xE84,0xE86,0xE90,0xE92,0xE98,0xE9A}) do
     log(string.format("TR %04X=%04X",a,sh:read_u16(a)))
   end
 end
end,"frame")
