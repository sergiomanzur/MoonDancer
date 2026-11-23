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
emu.register_frame_done(function()
 frame=frame+1
 tap:reinstall()
 if frame==1200 then
   for _,tag in ipairs({":textram",":tileram",":sprites",":paletteram"}) do
     local sh=manager.machine.memory.shares[tag]
     local name="build/mame_"..string.sub(tag,2)..".bin"
     local f=io.open(name,"wb")
     if f then
       for i=0,sh.size-1 do f:write(string.char(sh:read_u8(i))) end
       f:close()
       log("wrote "..name.." size "..sh.size)
     else
       log("could not open "..name)
     end
   end
 end
end,"frame")
