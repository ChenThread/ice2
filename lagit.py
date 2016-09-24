import sys, struct

lagframes = 5*20

hdr = sys.stdin.read(32)
assert hdr[:4] == "ICE2"
sys.stdout.write(hdr)

vid_backlog = []

while True:
	# video
	s = ""
	killvid = False
	while True:
		c = sys.stdin.read(1)
		if c == "":
			killvid = True
			break

		s += c
		c = ord(c)
		if c == 0xFF:
			break
		
		bh = c & 0x3F
		is_copy = (c & 0x80) != 0
		use_bg = (c & 0x40) != 0
		c = sys.stdin.read(1)
		s += c
		c = ord(c)
		by = c & 0x3F
		load_pal = (c & 0x40) != 0

		if is_copy:
			s += sys.stdin.read(4)
		else:
			s += sys.stdin.read(2)
			if load_pal:
				s += sys.stdin.read(1)

	if killvid:
		break

	vid_backlog.append(s)

	# audio
	audlen_s = sys.stdin.read(2)
	audlen, = struct.unpack("<H", audlen_s)
	audata = audlen_s + ("" if audlen <= 0 else sys.stdin.read((audlen+1)&~1))
	if len(vid_backlog) >= lagframes:
		sys.stdout.write(vid_backlog.pop(0))
		sys.stdout.write(audata)

