local frame=0
local n=0
local function log(s) emu.print_info("PROBE "..s) end
local cpu=manager.machine.devices[":maincpu"]
local os=cpu.spaces["opcodes"]
local tap=os:install_read_tap(0x1E54,0x1E55,"e31fetch",function(offset,data,mask)
  if n<20 then
    log(string.format("FETCH f=%d off=%06X data=%04X time=%s",frame,offset,data,tostring(emu.time())))
    n=n+1
  end
end)
emu.register_frame_done(function() frame=frame+1; tap:reinstall() end,"frame")
