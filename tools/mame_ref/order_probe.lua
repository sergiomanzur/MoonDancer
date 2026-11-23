local frame=0
local tap=nil
local wr=nil
local function log(s) emu.print_info("PROBE "..s) end
local cpu=manager.machine.devices[":maincpu"]
local space=cpu.spaces["program"]
local rdtap=space:install_read_tap(0xE40000,0xE4003F,"rd",function(offset,data,mask)
  if offset==0xE40008 and frame>=934 and frame<=943 then
    log(string.format("SVC f=%d data=%04X",frame,data))
  end
  return data,mask
end)
local function onwrite(offset,data,mask)
  if (offset & 0x3FFE)==0x2030 and frame>=934 and frame<=943 then
    log(string.format("E031W f=%d v=%02X",frame,data&0xff))
  end
end
emu.register_frame_done(function()
  frame=frame+1
  rdtap:reinstall()
  if frame==2 and tap==nil then tap=space:install_write_tap(0xFFE000,0xFFFFFF,"wr",onwrite)
  elseif tap~=nil then tap:reinstall() end
end,"frame")
