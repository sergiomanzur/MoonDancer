local frame=0
local function log(s) emu.print_info(s) end
emu.register_frame_done(function()
 frame=frame+1
 if frame~=2400 then return end
 local sh=manager.machine.memory.shares[":sprites"]
 local t={}; for i=0,63 do t[#t+1]=string.format("%04X",sh:read_u16(i*2)) end
 log("MAME sprites: "..table.concat(t," "))
 local nz=0; for i=0,sh.size-2,2 do if sh:read_u16(i)~=0 then nz=nz+1 end end
 log("MAME sprite nonzero words "..nz)
end,"frame")
