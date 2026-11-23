local frame=0
local regs={}
local function log(s) emu.print_info(s) end
local cpu=manager.machine.devices[":maincpu"]
local space=cpu.spaces["program"]
local tap=space:install_write_tap(0xC00000,0xC0000F,"vdp",function(offset,data,mask)
  if offset==0xC00004 or offset==0xC00006 then
    if (data & 0xC000)==0x8000 then regs[(data>>8)&0x3F]=data&0xFF end
  end
end)
local rtap=space:install_read_tap(0xE40000,0xE4003F,"io",function(offset,data,mask)
  if offset==0xE40008 then
    if frame>=900 and frame<940 then data=data & ~0x01 end
    if frame>=1000 and frame<1040 then data=data & ~0x10 end
  end
  return data,mask
end)
emu.register_frame_done(function()
 frame=frame+1
 tap:reinstall(); rtap:reinstall()
 if frame==1200 then
   local t={}
   for i=0,0x17 do if regs[i] then t[#t+1]=string.format("%02X=%02X",i,regs[i]) end end
   log("VDPREGS "..table.concat(t," "))
 end
end,"frame")
