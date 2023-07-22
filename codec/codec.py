import wave
import matplotlib.pyplot as plt
import numpy as np
from math import log, ceil

SAMPLERATE=96000
TIMEBASE=8

def chunker(seq, size):
    return (seq[pos:pos + size] for pos in range(0, len(seq), size))

def to_bytes(seq):
    return (int(''.join([chr(ord('0') + c) for c in x]), 2) for x in seq)

class Codec:
    timebase = 1
    state = 0

    def __init__(self, timebase=1):
        self.timebase = timebase

    def bitstream(self, octets):
        for o in octets:
            for b in range(8):
                bit = (o >> (7 - b)) & 1
                yield bit

    def to_nrzi(self, bits):
        for bit in bits:
            if bit:
                self.state = (self.state + 1) % 2
                for t in range(self.timebase):
                    yield self.state
            else:
                for t in range(self.timebase):
                    yield self.state

    def from_nrzi(self, nrzi):
        return [int(a != b) for a,b in zip(nrzi[1:], nrzi)]

    def encode(self, octets):
        return self.to_nrzi(self.bitstream(octets)) 

    def to_wav_levels(self, nrzi):
        return ([64,192][x] for x in nrzi)

'''
0 -> RN
1 -> RR
'''
class FM(Codec):
    def bitstream(self, octets):
        for o in octets:
            for b in range(8):
                bit = (o >> (7 - b)) & 1
                yield 1
                yield bit

'''
(x)1 -> NR
(0)0 -> RN
(1)0 -> NN
'''
class MFM(Codec):
    SYNC = [1,0,0,0,1,0,0,1,0,0,0,1,0,0,1]
    prev_bit = 0

    def bitstream(self, octets):
        for o in octets:
            for b in range(8):
                clock = 0
                bit = (o >> (7 - b)) & 1
                if bit == 0:
                    clock = (1 - self.prev_bit)
                self.prev_bit = bit

                yield clock
                yield bit

    '''
    mfm bitstream to bytes
    '''
    def decode(self, bstream):
        bits = bstream[1::2]
        print("decoded bits: ", bits[:100])
        return to_bytes(chunker(bits, 8))

    '''
    find sync marker in mfm bitstream
    100010010001001
    '''
    def scan_sync(self, bstream):
        n = 0
        synclen = len(MFM.SYNC)
        while n < len(bstream):
            if bstream[n:n+synclen] == MFM.SYNC:
                return n + synclen
            n += 1

        return n

'''
sanity check for MFM encoder/decoder
'''
def basic_mfm_test():
    mfm = MFM(1)
    mfm_raw = list(mfm.bitstream([0xA1]))
    print('mfm bitstream for 0xA1: ', mfm_raw)
    mfm_raw = MFM.SYNC + mfm_raw
    mfm_nrzi = list(mfm.to_nrzi(mfm_raw))
    print('mfm wav: ', mfm_nrzi)
    back_mfm = [int(a != b) for a,b in zip(mfm_nrzi[1:], mfm_nrzi)]
    print('restored mfm: ', back_mfm) # first bit lost, alas
    back_mfm = mfm.from_nrzi([0]*13 + mfm_nrzi)
    print('restored mfm: ', back_mfm) # first bit lost, alas

    sync_pos = mfm.scan_sync(back_mfm)
    print('sync pos: ', sync_pos)
    decoded_bits = mfm.decode(back_mfm[sync_pos:])
    print('decoded data: ', list(decoded_bits))


'''
Bit Pattern

11      RNNN
10      NRNN
011     NNRNNN
010     RNNRNN
000     NNNRNN
0010    NNRNNRNN
0011    NNNNRNNN
'''
class RLL27(Codec):
    bitpatterns = [
        [4, None],              # 0
        [2, None],              # 1
        [12, None],             # 2     10
        [13, None],             # 3     11
        [8, None],              # 4
        [6, None],              # 5
        [14, None],             # 6     010
        [15, None],             # 7     011
        [16, None],             # 8     000
        [10, None],             # 9
        [17, None],             # 10    0010
        [18, None],             # 11    0011
        [-1, '0100'],           # 12
        [-1, '1000'],           # 13
        [-1, '100100'],         # 14
        [-1, '001000'],         # 15
        [-1, '000100'],         # 16
        [-1, '00100100'],       # 17
        [-1, '00001000'],       # 18
        ]
    index = 0

    def __init__(self, timebase=1):
        super().__init__(timebase)
        self.index = 0
        
    def bitstream(self, octets):
        for o in super().bitstream(octets):
            self.index = self.bitpatterns[self.index + o][0]
            pattern = self.bitpatterns[self.index][1]
            if pattern != None:
                self.index = 0
                for p in pattern:
                    yield ord(p) - ord('0')

        # pad remaining sequence
        while self.index != 0:
            self.index = self.bitpatterns[self.index + 0][0]
            pattern = self.bitpatterns[self.index][1]
            if pattern != None:
                self.index = 0
                for p in pattern:
                    yield ord(p) - ord('0')



def get_testdata():
    #testdata = [0b11001010]
    #testdata = [ord(x) for x in 'DRINK FECK ARSE GIRLS']
    #testdata = [0x55,0x55]
    with open('test.txt', 'r') as file:
        testdata = (ord(x) for x in file.read())
    return testdata

def wav_writer(path):
    wav = wave.open(path, 'wb')
    wav.setnchannels(1)
    wav.setsampwidth(1)
    wav.setframerate(SAMPLERATE)
    return wav

def run_encoder(name, codec, data, wavpath):
    print(f'{name}:')
    preamble = [0xe5] * 320
    with wav_writer(wavpath) as wav:
        # preamble first
        wav.writeframes(bytes(codec.to_wav_levels(codec.encode(preamble))))
        # sync marker
        wav.writeframes(bytes(codec.to_wav_levels(codec.to_nrzi(codec.SYNC))))
        wav.writeframes(bytes(codec.to_wav_levels(codec.encode(data))))
    print()

# VCO set period = TIMEBASE for MFM (n samples from transition to transition)

# epic DLL algorithm by Lukas K. carrotindustries h/t
def sample_bits(all_bits, alpha=0.05, Ki=.0000004, Kp=0.005):
    debug = False

    #alpha = 0.05 # .006
    #Ki = .0000004
    #Kp = 0.005 #.001

    last_acc = 0
    lastbit = 0
    integ_max = 927681
    phase_delta = 0
    phase_delta_filtered = 0
    integ = 0
    acc_size = 1000
    ftw0 = acc_size / (TIMEBASE/2) #11.7 #42.7
    ftw = 0
    acc = acc_size / 2 #+ acc_size / 8
    lastbit = True
    sampled_bits = []

    # debug logs
    bits = []
    phase_deltas_filtered = []
    ftws = []
    integs = []
    accs = []

    #print(f'ftw0={ftw0}')

    #Ki = 0
    #Kp = 0
    #alpha = 1

    for i,bit in enumerate(all_bits):
        if debug :
            bits.append(bit)
        if bit != lastbit : # input transition
            phase_delta = (acc_size / 2 - acc)  # 180 deg off transition point
            #print(f'{i:3} TRANSITION phase_delta={phase_delta}')
        if acc < last_acc : # phase accumulator has wrapped around
            #print(f'{i:3}: SAMPLED {bit}')
            sampled_bits.append(bit)
        last_acc = acc
        phase_delta_filtered = phase_delta * alpha + phase_delta_filtered * (1 - alpha)
        #print(f'phase_delta_filtered={phase_delta_filtered} *Kp={phase_delta_filtered * Kp}')
        integ += phase_delta_filtered * Ki  # -- scale, then integrate
        #print(integ)
        if integ > integ_max :
            integ = integ_max
        elif integ < -integ_max :
            integ = -integ_max
        integs.append(integ)
        #integs.append(phase_delta_filtered * Ki)
        
        ftw = ftw0 + phase_delta_filtered * Kp + integ
        if debug:
            phase_deltas_filtered.append(phase_delta_filtered / acc_size)
        lastbit = bit
        acc = (acc + ftw) % acc_size
        #print(f'ftw={ftw} acc={acc}')
        #if debug and len(bits) > 200000 :
        #    break
        if debug :
            accs.append(acc/acc_size)
            ftws.append(ftw)

    if debug :
        plt.rcParams["figure.figsize"] = (15,4)
        #plt.plot(np.array(phase_deltas_filtered[::10000])*360)
        #plt.xlabel("sample")
        #plt.ylabel("filtered phase error [degree]")
        #plt.grid()
        #plt.show()

        #plt.xlim(len(bits)-400, len(bits))
        plt.xlim(0, 40000)
        #plt.plot(accs, label="VCO phase (normalized)")
        #plt.plot([0, len(bits)], [.5, .5], label="180°")
        #plt.plot(bits, label="bits")
        plt.plot(integs, label="integs")
        #plt.plot(np.array(ftws)-ftw0, label="freq tuning word")
        #plt.plot(np.array(phase_deltas_filtered), label="filtered phase error")
        plt.grid()
        plt.xlabel("sample")
        plt.legend()
        plt.show()


    return sampled_bits

# epic DLL algorithm by Lukas K. carrotindustries h/t
def isample_bits(all_bits, alpha=0.05, Ki=.0000004, Kp=0.005):
    last_acc = 0
    lastbit = 0
    phase_delta = 0
    phase_delta_filtered = 0
    integ = 0
    lastbit = True
    sampled_bits = []

    #print(f'ftw0={ftw0}')

    # baseline values
    # Kp = 0.093
    # Ki = 0.000137
    # alpha = 0.099

    # 12.20
    nscale = 20
    scale = 1 << nscale
    one = scale
    iKp = int(Kp * scale)
    iKi = int(Ki * scale)
    ialpha = int(alpha * scale)

    integ_max = 512 * scale

    acc_size = 512
    ftw0 = acc_size // (TIMEBASE//2) * scale
    ftw = 0
    iacc_size = acc_size * scale
    iacc = iacc_size // 2
    
    max_q = 0

    for i,bit in enumerate(all_bits):
        if bit != lastbit : # input transition
            phase_delta = iacc_size // 2 - iacc  # 180 deg off transition point
            #print(f'{i:3} TRANSITION phase_delta={phase_delta}')
        if iacc < last_acc : # phase accumulator has wrapped around
            #print(f'{i:3}: SAMPLED {bit}')
            sampled_bits.append(bit)
        last_acc = iacc

        phase_delta_filtered = phase_delta * ialpha + phase_delta_filtered * (one - ialpha)

        if phase_delta_filtered < 0:
            max_q = max(max_q, ceil(log(-phase_delta_filtered)/log(2)) + 1)
        elif phase_delta_filtered > 0:
            max_q = max(max_q, ceil(log(phase_delta_filtered)/log(2)))
        #print(f'phase_delta_filtered before scaling down: {phase_delta_filtered} q={q}')
        phase_delta_filtered = phase_delta_filtered >> nscale

        #print(f'phase_delta_filtered={phase_delta_filtered} *Kp={phase_delta_filtered * Kp}')

        integ += (phase_delta_filtered * iKi) >> nscale # -- scale, then integrate

        #print(integ)
        if integ > integ_max :
            integ = integ_max
        elif integ < -integ_max :
            integ = -integ_max
        
        ftw = ftw0 + ((phase_delta_filtered * iKp) >> nscale) + integ
        lastbit = bit
        iacc = (iacc + ftw) % iacc_size
        #print(f'ftw={ftw} acc={acc}')

    print(f'max_q={max_q}')


    return sampled_bits

def run_decoder(path):
    reference = list(get_testdata())

    Kp = 0.093
    #Ki = 0.000056
    Ki = 0.000137
    alpha = 0.099

    while Ki < 0.5: 
        with wave.open(path, 'rb') as wav:
            #print(f'wav has {wav.getnframes()} frames, sampwidth={wav.getsampwidth()}')
            wav_frames = wav.readframes(wav.getnframes())
            wav_bytes = [int(x > 128) for x in wav_frames]

            #print(wav_bytes[:100])
            sampled_bits = isample_bits(wav_bytes, Kp=Kp, Ki=Ki, alpha=alpha)
            #print(sampled_bits[:100])

            # print sampled bits
            #print(''.join([str(x) for x in sampled_bits[:100]]))
            #break
            
            mfm=MFM(timebase=TIMEBASE//2)
            back_mfm = mfm.from_nrzi(sampled_bits)

            # print mfm bits
            print(''.join([str(x) for x in back_mfm[:100]]))

            sync_pos = mfm.scan_sync(back_mfm)
            print('sync pos: ', sync_pos)
            decoded_bits = mfm.decode(back_mfm[sync_pos:])
            #break

            decoded_bits = list(decoded_bits)
            nerrors = 0
            for i in range(min(len(reference), len(decoded_bits))):
                if reference[i] != decoded_bits[i]:
                    nerrors += 1

            print(f'Kp={Kp} Ki={Ki} alpha={alpha} nerrors={nerrors} BER: {nerrors/len(reference) * 100:3.2}')

            print('decoded data: ', ''.join([chr(x) for x in decoded_bits]))
            break
            #Ki = Ki + .00001
            #Kp = Kp + 0.001
            #alpha = alpha + 0.01


pulse=2*TIMEBASE/SAMPLERATE
print(f'Samplerate: {SAMPLERATE} Timebase: {TIMEBASE} Short pulse: {pulse*1e6:4.2f}us, {1.0/pulse}Hz')

#run_encoder('MFM', MFM(timebase=TIMEBASE//2), get_testdata(), 'test_mfm.wav')

run_decoder('wow5percent.wav')


##print('bitstream=', list(mfm.bitstream(get_testdata())))
#print('mfm=', list(mfm.bitstream(get_testdata())))
#print('nrzi=', list(mfm.encode(get_testdata())))
#bits = mfm.encode(get_testdata())
#butts = list(bits)
##print("butts=", butts)
#sampled = sample_bits(butts)
#print('sampled=', sampled[:])
#
#nrz_bits = [int(a != b) for a,b in zip(sampled[1:], sampled)]
#print('nrz_bits=', nrz_bits)
#
#
##print('Timebase 1 mfm:')
##print(list(MFM(timebase=1).bitstream(get_testdata())))
