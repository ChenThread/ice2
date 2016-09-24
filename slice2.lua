local W, H = 160, 50

local component = require("component")
local term = require("term")
local gpu = component.gpu
local computer = require("computer")
local tape = component.tape_drive
local internet = component.internet

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

local addr, port = ...
port = tonumber(port)
local fp = internet.connect(addr, port)

local IBUF_LEN = 8192
local ibuf = ""
local ibuf_offs = 1
function get_byte()
	if ibuf_offs > #ibuf then
		ibuf_offs = 1
		while true do
			ibuf = fp.read(IBUF_LEN)
			if ibuf == "" then
				os.sleep(0.01)
			else
				break
			end
		end
	end

	local ret = ibuf:sub(ibuf_offs,ibuf_offs)
	ibuf_offs = ibuf_offs + 1
	return ret
end

function get_block(len)
	local ret = ""
	while ibuf_offs+len-#ret > #ibuf do
		ret = ret .. ibuf:sub(ibuf_offs)
		ibuf_offs = 1
		while true do
			ibuf = fp.read(IBUF_LEN)
			if ibuf == "" then
				os.sleep(0.01)
			else
				break
			end
		end
	end

	local new_ibuf_offs = ibuf_offs + (len-#ret)
	ret = ret .. ibuf:sub(ibuf_offs, ibuf_offs+(len-#ret)-1)
	ibuf_offs = new_ibuf_offs
	return ret
end

if sysnative then
	tlast = os.clock()
else
	os.sleep(0.05)
	--if tape then tape.play() end
	--os.sleep(1.0) -- deal to sound latency
	tlast = computer.uptime()
end

local tape_frames_remain = 0
local delay_acc = 0.0
local function delay(d)
	assert(d >= 0.0)
	delay_acc = delay_acc + d
	local dquo = math.floor(delay_acc / 0.05) * 0.05
	delay_acc = delay_acc - dquo
	os.sleep(dquo)
end

-- do header
assert(get_byte() == "I")
assert(get_byte() == "C")
assert(get_byte() == "E")
assert(get_byte() == "2")
assert(get_byte():byte() == 0x01)
assert(get_byte():byte() == 0x01)
assert(get_byte():byte() == 0x01)
assert(get_byte():byte() == 0x00)
assert(get_byte():byte() == 160)
assert(get_byte():byte() == 0)
assert(get_byte():byte() == 50)
assert(get_byte():byte() == 0)
assert(get_byte():byte() == 20)
assert(get_byte():byte() == 0)
assert(get_byte():byte() == 0)
assert(get_byte():byte() == 0)

-- check audio
acodec_idx = get_byte():byte()
aoutbuf = ""
assert(acodec_idx == 0x00 or acodec_idx == 0x0A)
assert(get_byte():byte() == 1)
assert(get_byte():byte() == 0)
assert(get_byte():byte() == 0)
assert(get_byte():byte() == (48000&0xFF))
assert(get_byte():byte() == (48000>>8))
assert(get_byte():byte() == 0)
assert(get_byte():byte() == 0)

assert(get_byte():byte() == 0)
assert(get_byte():byte() == 0)
assert(get_byte():byte() == 0)
assert(get_byte():byte() == 0)
assert(get_byte():byte() == 0)
assert(get_byte():byte() == 0)
assert(get_byte():byte() == 0)
assert(get_byte():byte() == 0)

if tape and acodec_idx ~= 0 then
	tape.play()
end

while true do
	local s = get_byte()
	if s == "" or s == nil then
		break
	end
	local c = s:byte()

	if c == 0xFF then
		-- add audio
		if acodec_idx ~= 0 then
			local al = get_byte():byte()
			local ah = get_byte():byte()
			local alen = al|(ah<<8)
			if alen > 0 then
				aoutbuf = aoutbuf .. get_block(alen)
			end

			local buf_gap = 6000*4
			local otpos = tape and tape.getPosition()
			if #aoutbuf >= buf_gap then
				local oblen = #aoutbuf
				oblen = oblen - (oblen%buf_gap)
				aoutcnk = aoutbuf:sub(1+(oblen-buf_gap), oblen)
				aoutbuf = aoutbuf:sub(1+oblen)
				if tape then
					--tape.stop()
					tape.seek(-(buf_gap*2+otpos))
					tape.write(aoutcnk)
					--tape.seek((-buf_gap)+otpos)
					tape.seek(#aoutcnk)
					--tape.play()
				end
			elseif tape then
				local new_otpos = otpos % buf_gap
				if new_otpos ~= otpos then
					tape.seek(new_otpos-otpos)
				end
			end
		end

		-- wait
		tnow = computer.uptime()
		tlast = tlast + 0.05
		while tnow < tlast do
			--delay(tlast-tnow)
			--os.sleep(tlast-tnow)
			os.sleep(0.01)
			tnow = computer.uptime()
		end

	else
		local bh = c & 0x3F
		local is_copy = ((c & 0x80) ~= 0)
		local use_bg = ((c & 0x40) ~= 0)
		c = get_byte():byte()
		local by = c & 0x3F
		local load_pal = ((c & 0x40) ~= 0)
		local bw = get_byte():byte()
		local bx = get_byte():byte()

		if is_copy then
			-- copy
			local dx = (get_byte():byte()~0x80)-0x80
			local dy = (get_byte():byte()~0x80)-0x80
			gpu.copy(bx+1, by+1, bw, bh, dx, dy)

		else 
			-- fill
			if load_pal then
				local p=get_byte():byte()
				local col = p
				if p >= 16 then col = COLMAP[p] end

				if use_bg then gpu.setBackground(col, (p<16))
				else gpu.setForeground(col, (p<16)) end
			end

			if bh == 1 then
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

