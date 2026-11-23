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
 if frame==1210 or frame==1211 then
   for _,tag in ipairs({":workram",":textram",":tileram",":sprites",":paletteram"}) do
     local sh=manager.machine.memory.shares[tag]
     if sh then
       local name=string.sub(tag,2)
       local f=io.open("build/mame"..frame.."_"..name..".bin","wb")
       if f then for i=0,sh.size-1 do f:write(string.char(sh:read_u8(i))) end f:close() end
     else log("missing share "..tag) end
   end
 end
end,"frame")
