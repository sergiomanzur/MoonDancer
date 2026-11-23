local frame=0
local n=0
local function log(s) emu.print_info("PROBE "..s) end
local cpu=manager.machine.devices[":maincpu"]
local space=cpu.spaces["program"]
local tap=space:install_write_tap(0x000000,0xFFFFFF,"wr",function(offset,data,mask)
  if (offset & 0x3FFF)==0x2031 and n<40 then
    log(string.format("W31 off=%06X data=%02X f=%d time=%s",offset,data&0xff,frame,tostring(manager.machine.time)))
    n=n+1
  end
end)
emu.register_frame_done(function() frame=frame+1; tap:reinstall() end,"frame")
