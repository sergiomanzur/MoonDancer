local frame=0
local function log(s) emu.print_info("PROBE "..s) end
local cpu=manager.machine.devices[":maincpu"]
local space=cpu.spaces["program"]
local rtap=space:install_read_tap(0xE40000,0xE4003F,"io",function(offset,data,mask)
  if offset==0xE40008 then
    if frame>=900 and frame<940 then data=data & ~0x01 end
    if frame>=1000 and frame<1040 then data=data & ~0x10 end
  elseif offset==0xE40000 then
    local p1=0xFF
    if frame>=1100 then
      if math.floor(frame/90)%2==1 then p1=p1 & ~0x40 else p1=p1 & ~0x80 end
      if frame%20<4 then p1=p1 & ~0x01 end
      if frame%120<30 then p1=p1 & ~0x20 end
    end
    data=(data & 0xFF00) | p1
  end
  return data,mask
end)
emu.register_frame_done(function()
 frame=frame+1; rtap:reinstall()
 if frame>=1120 and frame<=1160 then
   local sh=manager.machine.memory.shares[":workram"]
   local f=io.open("build/mdframe_"..frame..".bin","wb")
   if f then for i=0,sh.size-1 do f:write(string.char(sh:read_u8(i))) end f:close() end
 end
end,"frame")
