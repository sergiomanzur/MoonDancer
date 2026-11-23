local frame=0
local function log(s) emu.print_info("PROBE "..s) end
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
 local sh=manager.machine.memory.shares[":workram"]
 if frame>=940 and frame<=950 then
   log(string.format("E0F4 %d %02X%02X E031 %02X",frame,sh:read_u8(0x20F4),sh:read_u8(0x20F5),sh:read_u8(0x2031)))
 end
 if frame==946 or frame==947 then
   local f=io.open("build/mame_wr_"..frame..".bin","wb")
   if f then for i=0,sh.size-1 do f:write(string.char(sh:read_u8(i))) end f:close() end
 end
end,"frame")
