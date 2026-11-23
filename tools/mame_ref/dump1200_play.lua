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
 if frame==1200 then
   for _,tag in ipairs({":workram",":textram",":tileram",":palettes"}) do
     local sh=manager.machine.memory.shares[tag]
     if sh then
       local name=string.sub(tag,2)
       local f=io.open("build/mame1200_"..name..".bin","wb")
       if f then for i=0,sh.size-1 do f:write(string.char(sh:read_u8(i))) end f:close() end
     end
   end
   local wsh=manager.machine.memory.shares[":workram"]
   log(string.format("E0F4 %02X%02X E031 %02X",wsh:read_u8(0x20F4),wsh:read_u8(0x20F5),wsh:read_u8(0x2031)))
   log("dumped 1200")
 end
end,"frame")
