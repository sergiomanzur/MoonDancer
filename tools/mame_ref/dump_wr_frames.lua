local frame=0
local wanted={[1]=true,[5]=true,[10]=true,[20]=true,[50]=true,[100]=true,[200]=true,[400]=true,[600]=true,[900]=true,[1200]=true}
local function log(s) emu.print_info(s) end
emu.register_frame_done(function()
 frame=frame+1
 if wanted[frame] then
   local sh=manager.machine.memory.shares[":workram"]
   local f=io.open(string.format("build/mame_wr_%d.bin",frame),"wb")
   if f then for i=0,sh.size-1 do f:write(string.char(sh:read_u8(i))) end f:close(); log("wr "..frame) end
 end
end,"frame")
