local W, H = 160, 50

local component = require("component")
local term = require("term")
local gpu = component.gpu
local computer = require("computer")
local tape = nil--component.tape_drive

--BLOCKS = {" ", "▀", "▄", "█"}
local FGS, BGS = "",""
local FGC, BGC = "█"," "
local COLMAP = {}
local i
for i=1,160 do FGS, BGS = FGS..FGC, BGS..BGC end
for i=0,16-1 do
	COLMAP[i] = (((i+1)*255+32)//64)*0x010101
	gpu.setPaletteColor(i, COLMAP[i])
end
for i=0,240-1 do
	local g = i%8
	local r = (i//8)%6
	local b = (i//(6*8))

	r = (r*255+2)//5
	g = (g*255+3)//7
	b = (b*255+2)//4

	--print(r,g,b)
	COLMAP[i+16] = (r<<16)|(g<<8)|b
end

gpu.setBackground(0x000000)
gpu.setForeground(0xFFFFFF)
term.clear()

if tape then
	tape.stop()
	while tape.seek(-tape.getSize()) ~= 0 do
		os.sleep(0.05)
		tape.stop()
	end
	tape.stop()
end

local fname = ...
local fp = io.open(fname, "rb")

if sysnative then
	tlast = os.clock()
else
	os.sleep(0.05)
	if tape then tape.play() end
	os.sleep(1.0) -- deal to sound latency
	tlast = computer.uptime()
end

local delay_acc = 0.0
local function delay(d)
	assert(d >= 0.0)
	delay_acc = delay_acc + d
	local dquo = math.floor(delay_acc / 0.05) * 0.05
	delay_acc = delay_acc - dquo
	os.sleep(dquo)
end

while true do
	local s = fp:read(1)
	if s == "" or s == nil then
		break
	end
	local c = s:byte()

	if c == 0xFF then
		tnow = computer.uptime()
		tlast = tlast + 0.05
		while tnow < tlast do
			--delay(tlast-tnow)
			os.sleep(tlast-tnow)
			tnow = computer.uptime()
		end

	else
		local bh = c & 0x3F
		local is_copy = ((c & 0x80) ~= 0)
		local use_bg = ((c & 0x40) ~= 0)
		c = fp:read(1):byte()
		local by = c & 0x3F
		local load_pal = ((c & 0x40) ~= 0)
		local bw = fp:read(1):byte()
		local bx = fp:read(1):byte()

		if is_copy then
			-- copy
			local dx = (fp:read(1):byte()~0x80)-0x80
			local dy = (fp:read(1):byte()~0x80)-0x80
			gpu.copy(bx+1, by+1, bw, bh, dx, dy)

		else 
			-- fill
			if load_pal then
				local p=fp:read(1):byte()
				local col = p
				if p >= 16 then col = COLMAP[p] end

				if use_bg then gpu.setBackground(col, (p<16))
				else gpu.setForeground(col, (p<16)) end
			end

			if bh == -2 then
				gpu.set(bx+1, by+1, ((use_bg and BGS) or FGS):sub(
					1, bw*#((use_bg and BGC) or FGC)))
			else
				gpu.fill(bx+1, by+1, bw, bh, ((use_bg and BGC) or FGC))
			end
		end
	end
end

gpu.setBackground(0x000000)
gpu.setForeground(0xFFFFFF)

