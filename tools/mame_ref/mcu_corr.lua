local frame=0
local nx=0
local np=0
local function log(s) emu.print_info("PROBE "..s) end
local mcu=manager.machine.devices[":mcu"]
local cpu=manager.machine.devices[":maincpu"]
local dspace=mcu.spaces["xdata"]
local pspace=cpu.spaces["program"]
local function onx(offset,data,mask)
  if frame>=1109 and frame<=1112 and nx<120 then
    log(string.format("XW f=%d off=%02X v=%02X pc=%04X",frame,offset,data,mask,mcu.state["CURPC"].value)); nx=nx+1
  end
end
local function onp(offset,data,mask)
  if frame>=1109 and frame<=1112 and np<40 then
    log(string.format("PW f=%d off=%06X v=%04X pc=%06X mcupc=%04X",frame,offset,data,cpu.state["CURPC"].value,mcu.state["CURPC"].value)); np=np+1
  end
end
local xt=dspace:install_write_tap(0x0000,0x00FF,"xw",onx)
local pt=pspace:install_write_tap(0xFFE000,0xFFFFFF,"pw",onp)
emu.register_frame_done(function() frame=frame+1; xt:reinstall(); pt:reinstall() end,"frame")
