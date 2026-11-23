local frame=0
local function log(s) emu.print_info("PROBE "..s) end
local cpu=manager.machine.devices[":maincpu"]
local space=cpu.spaces["program"]
local tap=space:install_write_tap(0xFFE000,0xFFEFFF,"wr",function(offset,data,mask)
  if offset==0x31 then
    log(string.format("TAP f=%d time=%s data=%02X",frame,tostring(manager.machine.time),data))
  end
end)
emu.register_frame_done(function()
  if frame<5 then log(string.format("FD f=%d time=%s",frame,tostring(manager.machine.time))) end
  frame=frame+1; tap:reinstall()
end,"frame")
