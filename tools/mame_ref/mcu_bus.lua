local frame=0
local n=0
local function log(s) emu.print_info("PROBE "..s) end
local mcu=manager.machine.devices[":mcu"]
local dspace=mcu.spaces["xdata"]
local function onwrite(offset,data,mask)
  if frame>=1095 and frame<=1099 and n<200 then
    log(string.format("MCUW f=%d off=%02X v=%02X mask=%02X pc=%04X",frame,offset,data,mask,mcu.state["CURPC"].value)); n=n+1
  end
end
local tap=dspace:install_write_tap(0x0000,0x00FF,"mcuwr",onwrite)
emu.register_frame_done(function() frame=frame+1; tap:reinstall() end,"frame")
