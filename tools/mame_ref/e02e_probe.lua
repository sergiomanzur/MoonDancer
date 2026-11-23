local frame=0
local n=0
local tap=nil
local last=-1
local function log(s) emu.print_info("PROBE "..s) end
local cpu=manager.machine.devices[":maincpu"]
local space=cpu.spaces["program"]
local function onwrite(offset,data,mask)
  if (offset & 0x3FFE)==0x202E and n<60 then
    log(string.format("W202E f=%d off=%06X v=%02X pc=%06X",frame,offset,data&0xff,cpu.state["CURPC"].value)); n=n+1
  end
end
emu.register_frame_done(function()
 frame=frame+1
 local sh=manager.machine.memory.shares[":workram"]
 local v=sh:read_u8(0x202E)
 if v~=last then log(string.format("E02E f=%d v=%02X",frame,v)); last=v end
 if tap==nil then tap=space:install_write_tap(0xFFE000,0xFFFFFF,"wr",onwrite)
 else tap:reinstall() end
end,"frame")
