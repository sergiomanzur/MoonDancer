local frame=0
local n=0
local tap=nil
local last=-1
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
local function onwrite(offset,data,mask)
  if (offset & 0x3FFE)==0x202E and n<80 then
    log(string.format("W202E f=%d v=%02X pc=%06X",frame,data&0xff,cpu.state["CURPC"].value)); n=n+1
  end
end
emu.register_frame_done(function()
 frame=frame+1; rtap:reinstall()
 local sh=manager.machine.memory.shares[":workram"]
 local v=sh:read_u8(0x202E)
 if v~=last then log(string.format("E02E f=%d v=%02X",frame,v)); last=v end
 if tap==nil then tap=space:install_write_tap(0xFFE000,0xFFFFFF,"wr",onwrite)
 else tap:reinstall() end
end,"frame")
