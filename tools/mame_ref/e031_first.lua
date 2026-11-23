local frame=0
local n=0
local tap=nil
local function log(s) emu.print_info("PROBE "..s) end
local cpu=manager.machine.devices[":maincpu"]
local space=cpu.spaces["program"]
local function onwrite(offset,data,mask)
  if (offset & 0x3FFE)==0x2030 and n<20 then
    log(string.format("E031W f=%d off=%06X v=%02X",frame,offset,data&0xff)); n=n+1
  end
end
emu.register_frame_done(function()
  frame=frame+1
  if tap==nil then tap=space:install_write_tap(0xFFE000,0xFFFFFF,"wr",onwrite)
  else tap:reinstall() end
end,"frame")
