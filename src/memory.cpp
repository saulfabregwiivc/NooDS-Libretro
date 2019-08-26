/*
    Copyright 2019 Hydr8gon

    This file is part of NooDS.

    NooDS is free software: you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    NooDS is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
    General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with NooDS. If not, see <https://www.gnu.org/licenses/>.
*/

#include <cmath>
#include <cstdio>
#include <cstring>

#include "memory.h"
#include "cartridge.h"
#include "core.h"
#include "cp15.h"
#include "fifo.h"
#include "gpu.h"
#include "rtc.h"
#include "spi.h"

namespace memory
{

uint8_t ram[0x400000];    //  4MB main RAM
uint8_t wram[0x8000];     // 32KB shared WRAM
uint8_t instrTcm[0x8000]; // 32KB instruction TCM
uint8_t dataTcm[0x4000];  // 16KB data TCM
uint8_t bios9[0x8000];    // 32KB ARM9 BIOS
uint8_t bios7[0x4000];    // 16KB ARM7 BIOS
uint8_t wram7[0x10000];   // 64KB ARM7 WRAM

uint8_t palette[0x800]; //   2KB palette
uint8_t vramA[0x20000]; // 128KB VRAM block A
uint8_t vramB[0x20000]; // 128KB VRAM block B
uint8_t vramC[0x20000]; // 128KB VRAM block C
uint8_t vramD[0x20000]; // 128KB VRAM block D
uint8_t vramE[0x10000]; //  64KB VRAM block E
uint8_t vramF[0x4000];  //  16KB VRAM block F
uint8_t vramG[0x4000];  //  16KB VRAM block G
uint8_t vramH[0x8000];  //  32KB VRAM block H
uint8_t vramI[0x4000];  //  16KB VRAM block I
uint8_t oam[0x800];     //   2KB OAM

uint16_t wramOffset9, wramSize9;
uint16_t wramOffset7, wramSize7;

uint32_t vramBases[9];
uint16_t *extPalettesA[5], *extPalettesB[5];

uint8_t ioData9[0x2000], ioMask9[0x2000], ioWriteMask9[0x2000];
uint8_t ioData7[0x2000], ioMask7[0x2000], ioWriteMask7[0x2000];

uint16_t *extkeyin = (uint16_t*)&ioData7[0x136];

uint16_t *spicnt  = (uint16_t*)&ioData7[0x1C0];
uint16_t *spidata = (uint16_t*)&ioData7[0x1C2];

uint16_t *divcnt   = (uint16_t*)&ioData9[0x280];
int64_t *divnumer  =  (int64_t*)&ioData9[0x290];
int64_t *divdenom  =  (int64_t*)&ioData9[0x298];
int64_t *divresult =  (int64_t*)&ioData9[0x2A0];
int64_t *divremain =  (int64_t*)&ioData9[0x2A8];

uint16_t *sqrtcnt    = (uint16_t*)&ioData9[0x2B0];
uint32_t *sqrtresult = (uint32_t*)&ioData9[0x2B4];
uint64_t *sqrtparam  = (uint64_t*)&ioData9[0x2B8];

uint16_t *powcnt1 = (uint16_t*)&ioData9[0x304];

void *vramMap(uint32_t address)
{
    // Get a pointer to the VRAM currently mapped to a given address
    if (vramBases[0] && address >= vramBases[0] && address < vramBases[0] + 0x20000) // 128KB VRAM block A
        return &vramA[address - vramBases[0]];
    else if (vramBases[1] && address >= vramBases[1] && address < vramBases[1] + 0x20000) // 128KB VRAM block B
        return &vramB[address - vramBases[1]];
    else if (vramBases[2] && address >= vramBases[2] && address < vramBases[2] + 0x20000) // 128KB VRAM block C
        return &vramC[address - vramBases[2]];
    else if (vramBases[3] && address >= vramBases[3] && address < vramBases[3] + 0x20000) // 128KB VRAM block D
        return &vramD[address - vramBases[3]];
    else if (vramBases[4] && address >= vramBases[4] && address < vramBases[4] + 0x10000) // 64KB VRAM block E
        return &vramE[address - vramBases[4]];
    else if (vramBases[5] && address >= vramBases[5] && address < vramBases[5] + 0x4000) // 16KB VRAM block F
        return &vramF[address - vramBases[5]];
    else if (vramBases[6] && address >= vramBases[6] && address < vramBases[6] + 0x4000) // 16KB VRAM block G
        return &vramG[address - vramBases[6]];
    else if (vramBases[7] && address >= vramBases[7] && address < vramBases[7] + 0x8000) // 32KB VRAM block H
        return &vramH[address - vramBases[7]];
    else if (vramBases[8] && address >= vramBases[8] && address < vramBases[8] + 0x4000) // 16KB VRAM block I
        return &vramI[address - vramBases[8]];

    return nullptr;
}

void *memoryMap9(uint32_t address, bool read)
{
    // Get a pointer to the memory currently mapped to a given address on the ARM9
    if (cp15::itcmEnable && address < cp15::itcmSize) // 32KB instruction TCM
        return &instrTcm[address % 0x8000];
    else if (cp15::dtcmEnable && address >= cp15::dtcmBase && address < cp15::dtcmBase + cp15::dtcmSize) // 16KB data TCM
        return &dataTcm[(address - cp15::dtcmBase) % 0x4000];
    else if (address >= 0x2000000 && address < 0x3000000) // 4MB main RAM
        return &ram[address % 0x400000];
    else if (address >= 0x3000000 && address < 0x4000000 && wramSize9 != 0) // 32KB shared WRAM
        return &wram[wramOffset9 + address % wramSize9];
    else if (address >= 0x5000000 && address < 0x6000000) // 2KB palette
        return &palette[address % 0x800];
    else if (address >= 0x6000000 && address < 0x7000000) // VRAM
        return vramMap(address);
    else if (address >= 0x7000000 && address < 0x8000000) // 2KB OAM
        return &oam[address % 0x800];
    else if (address >= 0xFFFF0000 && address < 0xFFFF8000 && read) // 32KB ARM9 BIOS
        return &bios9[address - 0xFFFF0000];

    return nullptr;
}

void *memoryMap7(uint32_t address, bool read)
{
    // Get a pointer to the memory currently mapped to a given address on the ARM7
    if (address < 0x4000 && read) // 16KB ARM7 BIOS
        return &bios7[address];
    else if (address >= 0x2000000 && address < 0x3000000) // 4MB main RAM
        return &ram[address % 0x400000];
    else if (address >= 0x3000000 && address < 0x3800000 && wramSize7 != 0) // 32KB shared WRAM
        return &wram[wramOffset7 + address % wramSize7];
    else if (address >= 0x3000000 && address < 0x4000000) // 64KB ARM7 WRAM
        return &wram7[address % 0x10000];

    return nullptr;
}

template <typename T> T ioRead9(uint32_t address)
{
    uint32_t ioAddr = address - 0x4000000;

    // Read from special transfer registers
    if (ioAddr == 0x100000) // IPCFIFORECV
        return fifo::receive(&interpreter::arm9, &interpreter::arm7);
    else if (ioAddr == 0x100010) // ROMDATAIN
        return cartridge::transfer(&interpreter::arm9);

    // Make sure an I/O register exists at the given address
    if (ioAddr >= sizeof(ioMask9) || !ioMask9[ioAddr])
    {
        printf("Unknown ARM9 I/O read: 0x%X\n", address);
        return 0;
    }

    // Read data from the ARM9 I/O registers
    return *(T*)&ioData9[ioAddr];
}

template <typename T> void ioWrite9(uint32_t address, T value)
{
    uint32_t ioAddr = address - 0x4000000;

    // Make sure an I/O register exists at the given address
    if (ioAddr >= sizeof(ioMask9) || !ioMask9[ioAddr])
    {
        printf("Unknown ARM9 I/O write: 0x%X\n", address);
        return;
    }

    // Write data to the ARM9 I/O registers
    // Only bits that are set in the write mask are modified
    *(T*)&ioData9[ioAddr] &= ~(*(T*)&ioWriteMask9[ioAddr]);
    *(T*)&ioData9[ioAddr] |= (value & *(T*)&ioWriteMask9[ioAddr]);

    // Handle special cases
    for (unsigned int i = 0; i < sizeof(T); i++)
    {
        switch (ioAddr + i)
        {
            case 0x0BB: case 0x0C7: case 0x0D3: case 0x0DF: // DMACNT_9
            {
                // Reload the DMA address counters if the enable bit changes from 0 to 1
                if (!(ioData9[ioAddr + i] & BIT(7)) && (((uint8_t*)&value)[i] & BIT(7)))
                {
                    uint8_t channel = (ioAddr + i - 0x0BB) / 0x0C;
                    interpreter::arm9.dmaDstAddrs[channel] = *interpreter::arm9.dmadad[channel];
                    interpreter::arm9.dmaSrcAddrs[channel] = *interpreter::arm9.dmasad[channel];
                }

                // Now that the old enable bit has been used, set the new one
                ioData9[ioAddr + i] &= ~BIT(7);
                ioData9[ioAddr + i] |= (((uint8_t*)&value)[i] & BIT(7));

                break;
            }

            case 0x100: case 0x104: case 0x108: case 0x10C: // TMCNT_L_9
            {
                // Redirect the write to the appropriate timer reload value
                ((uint8_t*)&interpreter::arm9.timerReloads[(ioAddr + i - 0x100) / 4])[0] = ((uint8_t*)&value)[i];
                break;
            }

            case 0x101: case 0x105: case 0x109: case 0x10D: // TMCNT_L_9
            {
                // Redirect the write to the appropriate timer reload value
                ((uint8_t*)&interpreter::arm9.timerReloads[(ioAddr + i - 0x101) / 4])[1] = ((uint8_t*)&value)[i];
                break;
            }

            case 0x102: case 0x106: case 0x10A: case 0x10E: // TMCNT_H_9
            {
                // Reload the appropriate timer counter if the enable bit changes from 0 to 1
                uint8_t timer = (ioAddr + i - 0x102) / 4;
                if (!(ioData9[ioAddr + i] & BIT(7)) && (((uint8_t*)&value)[i] & BIT(7)))
                    *interpreter::arm9.tmcntL[timer] = interpreter::arm9.timerReloads[timer];

                // Now that the old enable bit has been used, set the new one
                ioData9[ioAddr + i] &= ~BIT(7);
                ioData9[ioAddr + i] |= (((uint8_t*)&value)[i] & BIT(7));

                break;
            }

            case 0x181: // IPCSYNC_9
            {
                // Copy the ARM9 send value to the ARM7 receive value
                ioData7[0x180] = (((uint8_t*)&value)[i] & 0x0F);

                // Trigger a remote IRQ if enabled on both sides
                if ((((uint8_t*)&value)[i] & BIT(5)) && (ioData7[0x181] & BIT(8)))
                    *interpreter::arm7.irf |= BIT(16);

                break;
            }

            case 0x184: // IPCFIFOCNT_9
            {
                // Trigger a send FIFO empty IRQ if the FIFO is empty and the enable bit changes from 0 to 1
                if ((ioData9[0x184] & BIT(0)) && !(ioData9[0x184] & BIT(2)) && (((uint8_t*)&value)[i] & BIT(2)))
                    *interpreter::arm9.irf |= BIT(17); // Send FIFO empty IRQ

                // Now that the old enable bit has been used, set the new one
                ioData9[0x184] &= ~BIT(2);
                ioData9[0x184] |= (((uint8_t*)&value)[i] & BIT(2));

                // Clear the send FIFO if the clear bit is set
                if (((uint8_t*)&value)[i] & BIT(3))
                    fifo::clear(&interpreter::arm9, &interpreter::arm7);

                break;
            }

            case 0x185: // IPCFIFOCNT_9
            {
                // Trigger a receive FIFO not empty IRQ if the FIFO isn't empty and the enable bit changes from 0 to 1
                if (!(ioData9[0x185] & BIT(0)) && !(ioData9[0x185] & BIT(2)) && (((uint8_t*)&value)[i] & BIT(2)))
                    *interpreter::arm9.irf |= BIT(18);

                // Now that the old enable bit has been used, set the new one
                ioData9[0x185] &= ~BIT(2);
                ioData9[0x185] |= (((uint8_t*)&value)[i] & BIT(2));

                // If the error bit is set, acknowledge the error by clearing it
                if (((uint8_t*)&value)[i] & BIT(6))
                    ioData9[0x185] &= ~BIT(6);

                break;
            }

            case 0x188: case 0x189: case 0x18A: case 0x18B: // IPCFIFOSEND_9
            {
                // Trigger a FIFO send, and return so it doesn't trigger multiple times
                fifo::send(&interpreter::arm9, &interpreter::arm7);
                return;
            }

            case 0x1A2: // AUXSPIDATA_9
            {
                // Send the value to the AUX SPI
                spi::auxWrite(&interpreter::arm9, ((uint8_t*)&value)[i]);
                break;
            }

            case 0x1A7: // ROMCTRL_9
            {
                // Set the release reset bit, but never clear it
                ioData9[0x1A7] |= (((uint8_t*)&value)[i] & BIT(5));

                // Save the old start bit for later use and set the new one
                uint8_t startBit = (ioData9[0x1A7] & BIT(7));
                ioData9[0x1A7] &= ~BIT(7);
                ioData9[0x1A7] |= (((uint8_t*)&value)[i] & BIT(7));

                // Start a ROM transfer if the start bit changes from 0 to 1
                if (!startBit && (((uint8_t*)&value)[i] & BIT(7)))
                    cartridge::transferStart(&interpreter::arm9);

                break;
            }

            case 0x214: case 0x215: case 0x216: case 0x217: // IRF_9
            {
                // Acknowledge interrupts by clearing set bits
                ioData9[ioAddr + i] &= ~((uint8_t*)&value)[i];
                break;
            }

            case 0x240: // VRAMCNT_A
            {
                // Remap VRAM block A
                vramBases[0] = 0;
                if (((uint8_t*)&value)[i] & BIT(7)) // VRAM enabled
                {
                    uint8_t mst = (((uint8_t*)&value)[i] & 0x03);
                    uint8_t ofs = (((uint8_t*)&value)[i] & 0x18) >> 3;
                    switch (mst)
                    {
                        case 0: vramBases[0] = 0x6800000;                            break; // Plain ARM9 access
                        case 1: vramBases[0] = 0x6000000 + 0x20000 * ofs;            break; // Engine A BG VRAM
                        case 2: vramBases[0] = 0x6400000 + 0x20000 * (ofs & BIT(0)); break; // Engine A OBJ VRAM

                        default:
                        {
                            printf("Unknown VRAM A MST: %d\n", mst);
                            break;
                        }
                    }
                }
                break;
            }

            case 0x241: // VRAMCNT_B
            {
                // Remap VRAM block B
                vramBases[1] = 0;
                if (((uint8_t*)&value)[i] & BIT(7)) // VRAM enabled
                {
                    uint8_t mst = (((uint8_t*)&value)[i] & 0x03);
                    uint8_t ofs = (((uint8_t*)&value)[i] & 0x18) >> 3;
                    switch (mst)
                    {
                        case 0: vramBases[1] = 0x6820000;                            break; // Plain ARM9 access
                        case 1: vramBases[1] = 0x6000000 + 0x20000 * ofs;            break; // Engine A BG VRAM
                        case 2: vramBases[1] = 0x6400000 + 0x20000 * (ofs & BIT(0)); break; // Engine A OBJ VRAM

                        default:
                        {
                            printf("Unknown VRAM B MST: %d\n", mst);
                            break;
                        }
                    }
                }
                break;
            }

            case 0x242: // VRAMCNT_C
            {
                // Remap VRAM block C
                vramBases[2] = 0;
                if (((uint8_t*)&value)[i] & BIT(7)) // VRAM enabled
                {
                    uint8_t mst = (((uint8_t*)&value)[i] & 0x07);
                    uint8_t ofs = (((uint8_t*)&value)[i] & 0x18) >> 3;
                    switch (mst)
                    {
                        case 0: vramBases[2] = 0x6840000;                 break; // Plain ARM9 access
                        case 1: vramBases[2] = 0x6000000 + 0x20000 * ofs; break; // Engine A BG VRAM
                        case 4: vramBases[2] = 0x6200000;                 break; // Engine B BG VRAM

                        default:
                        {
                            printf("Unknown VRAM C MST: %d\n", mst);
                            break;
                        }
                    }
                }
                break;
            }

            case 0x243: // VRAMCNT_D
            {
                // Remap VRAM block D
                vramBases[3] = 0;
                if (((uint8_t*)&value)[i] & BIT(7)) // VRAM enabled
                {
                    uint8_t mst = (((uint8_t*)&value)[i] & 0x07);
                    uint8_t ofs = (((uint8_t*)&value)[i] & 0x18) >> 3;
                    switch (mst)
                    {
                        case 0: vramBases[3] = 0x6860000;                 break; // Plain ARM9 access
                        case 1: vramBases[3] = 0x6000000 + 0x20000 * ofs; break; // Engine A BG VRAM
                        case 4: vramBases[3] = 0x6600000;                 break; // Engine B OBJ VRAM

                        default:
                        {
                            printf("Unknown VRAM D MST: %d\n", mst);
                            break;
                        }
                    }
                }
                break;
            }

            case 0x244: // VRAMCNT_E
            {
                // Remap VRAM block E
                vramBases[4] = 0;
                if (((uint8_t*)&value)[i] & BIT(7)) // VRAM enabled
                {
                    uint8_t mst = (((uint8_t*)&value)[i] & 0x07);
                    switch (mst)
                    {
                        case 0: vramBases[4] = 0x6880000; break; // Plain ARM9 access
                        case 1: vramBases[4] = 0x6000000; break; // Engine A BG VRAM
                        case 2: vramBases[4] = 0x6400000; break; // Engine A OBJ VRAM

                        case 4: // Engine A BG ext palette
                        {
                            for (int i = 0; i < 4; i++)
                                extPalettesA[i] = (uint16_t*)&vramE[0x2000 * i];
                            break;
                        }

                        default:
                        {
                            printf("Unknown VRAM E MST: %d\n", mst);
                            break;
                        }
                    }
                }
                break;
            }

            case 0x245: // VRAMCNT_F
            {
                // Remap VRAM block F
                vramBases[5] = 0;
                if (((uint8_t*)&value)[i] & BIT(7)) // VRAM enabled
                {
                    uint8_t mst = (((uint8_t*)&value)[i] & 0x07);
                    uint8_t ofs = (((uint8_t*)&value)[i] & 0x18) >> 3;
                    switch (mst)
                    {
                        case 0: vramBases[5] = 0x6890000;                                                     break; // Plain ARM9 access
                        case 1: vramBases[5] = 0x6000000 + 0x8000 * (ofs & BIT(1)) + 0x4000 * (ofs & BIT(0)); break; // Engine A BG VRAM
                        case 2: vramBases[5] = 0x6400000 + 0x8000 * (ofs & BIT(1)) + 0x4000 * (ofs & BIT(0)); break; // Engine A OBJ VRAM
                        case 5: extPalettesA[4] = (uint16_t*)vramF;                                           break; // Engine A OBJ ext palette

                        case 4: // Engine A BG ext palette
                        {
                            for (int i = 0; i < 2; i++)
                                extPalettesA[(ofs & BIT(0)) * 2 + i] = (uint16_t*)&vramF[0x2000 * i];
                            break;
                        }

                        default:
                        {
                            printf("Unknown VRAM F MST: %d\n", mst);
                            break;
                        }
                    }
                }
                break;
            }

            case 0x246: // VRAMCNT_G
            {
                // Remap VRAM block G
                vramBases[6] = 0;
                if (((uint8_t*)&value)[i] & BIT(7)) // VRAM enabled
                {
                    uint8_t mst = (((uint8_t*)&value)[i] & 0x07);
                    uint8_t ofs = (((uint8_t*)&value)[i] & 0x18) >> 3;
                    switch (mst)
                    {
                        case 0: vramBases[6] = 0x6894000;                                                     break; // Plain ARM9 access
                        case 1: vramBases[6] = 0x6000000 + 0x8000 * (ofs & BIT(1)) + 0x4000 * (ofs & BIT(0)); break; // Engine A BG VRAM
                        case 2: vramBases[6] = 0x6400000 + 0x8000 * (ofs & BIT(1)) + 0x4000 * (ofs & BIT(0)); break; // Engine A OBJ VRAM
                        case 5: extPalettesA[4] = (uint16_t*)vramG;                                           break; // Engine A OBJ ext palette

                        case 4: // Engine A BG ext palette
                        {
                            for (int i = 0; i < 2; i++)
                                extPalettesA[(ofs & BIT(0)) * 2 + i] = (uint16_t*)&vramG[0x2000 * i];
                            break;
                        }

                        default:
                        {
                            printf("Unknown VRAM G MST: %d\n", mst);
                            break;
                        }
                    }
                }
                break;
            }

            case 0x247: // WRAMCNT
            {
                // Remap the shared WRAM
                switch (((uint8_t*)&value)[i] & 0x03)
                {
                    case 0: wramOffset9 = 0x0000; wramSize9 = 0x8000; wramOffset7 = 0x0000; wramSize7 = 0x0000; break;
                    case 1: wramOffset9 = 0x4000; wramSize9 = 0x4000; wramOffset7 = 0x0000; wramSize7 = 0x4000; break;
                    case 2: wramOffset9 = 0x0000; wramSize9 = 0x4000; wramOffset7 = 0x4000; wramSize7 = 0x4000; break;
                    case 3: wramOffset9 = 0x0000; wramSize9 = 0x0000; wramOffset7 = 0x0000; wramSize7 = 0x8000; break;
                }
                break;
            }

            case 0x248: // VRAMCNT_H
            {
                // Remap VRAM block H
                vramBases[7] = 0;
                if (((uint8_t*)&value)[i] & BIT(7)) // VRAM enabled
                {
                    uint8_t mst = (((uint8_t*)&value)[i] & 0x03);
                    switch (mst)
                    {
                        case 0: vramBases[7] = 0x6898000; break; // Plain ARM9 access
                        case 1: vramBases[7] = 0x6200000; break; // Engine B BG VRAM

                        case 2: // Engine B BG ext palette
                        {
                            for (int i = 0; i < 4; i++)
                                extPalettesB[i] = (uint16_t*)&vramH[0x2000 * i];
                            break;
                        }

                        default:
                        {
                            printf("Unknown VRAM H MST: %d\n", mst);
                            break;
                        }
                    }
                }

                break;
            }

            case 0x249: // VRAMCNT_I
            {
                // Remap VRAM block I
                vramBases[8] = 0;
                if (((uint8_t*)&value)[i] & BIT(7)) // VRAM enabled
                {
                    uint8_t mst = (((uint8_t*)&value)[i] & 0x03);
                    switch (mst)
                    {
                        case 0: vramBases[8] = 0x68A0000;           break; // Plain ARM9 access
                        case 1: vramBases[8] = 0x6208000;           break; // Engine B BG VRAM
                        case 2: vramBases[8] = 0x6600000;           break; // Engine B OBJ VRAM
                        case 3: extPalettesB[4] = (uint16_t*)vramI; break; // Engine B OBJ ext palette

                        default:
                        {
                            printf("Unknown VRAM I MST: %d\n", mst);
                            break;
                        }
                    }
                }

                break;
            }

            case 0x280: case 0x290: case 0x298: // DIV
            {
                // Clear the division by zero error bit
                *divcnt &= ~BIT(14);

                // Set the result and the remainder
                switch (*divcnt & 0x0003) // Division mode
                {
                    case 0: // 32-bit / 32-bit
                    {
                        if (*(int32_t*)divdenom == 0)
                        {
                            // Set the division by zero error bit
                            *divcnt |= BIT(14);
                        }
                        else
                        {
                            *divresult = *(int32_t*)divnumer / *(int32_t*)divdenom;
                            *divremain = *(int32_t*)divnumer % *(int32_t*)divdenom;
                        }
                        break;
                    }

                    case 1: case 3: // 64-bit / 32-bit
                    {
                        if (*(int32_t*)divdenom == 0)
                        {
                            // Set the division by zero error bit
                            *divcnt |= BIT(14);
                        }
                        else
                        {
                            *divresult = *divnumer / *(int32_t*)divdenom;
                            *divremain = *divnumer % *(int32_t*)divdenom;
                        }
                        break;
                    }

                    case 2: // 64-bit / 64-bit
                    {
                        if (*divdenom == 0)
                        {
                            // Set the division by zero error bit
                            *divcnt |= BIT(14);
                        }
                        else
                        {
                            *divresult = *divnumer / *divdenom;
                            *divremain = *divnumer % *divdenom;
                        }
                        break;
                    }
                }
                break;
            }

            case 0x2B0: case 0x2B8: // SQRT
            {
                // Set the square root result
                if (*sqrtcnt & BIT(0)) // 64-bit
                    *sqrtresult = sqrt(*sqrtparam);
                else // 32-bit
                    *sqrtresult = sqrt(*(uint32_t*)sqrtparam);
                break;
            }

            case 0x300: // POSTFLG_9
            {
                // Set the POSTFLG bit, but never clear it
                ioData9[0x300] |= (((uint8_t*)&value)[i] & 0x01);
                break;
            }
        }
    }
}

template <typename T> T ioRead7(uint32_t address)
{
    uint32_t ioAddr = address - 0x4000000;

    // Read from special transfer registers
    if (ioAddr == 0x100000) // IPCFIFORECV
        return fifo::receive(&interpreter::arm7, &interpreter::arm9);
    else if (ioAddr == 0x100010) // ROMDATAIN
        return cartridge::transfer(&interpreter::arm7);

    // Make sure an I/O register exists at the given address
    if (ioAddr >= sizeof(ioMask7) || !ioMask7[ioAddr])
    {
        printf("Unknown ARM7 I/O read: 0x%X\n", address);
        return 0;
    }

    // Read data from the ARM7 I/O registers
    return *(T*)&ioData7[ioAddr];
}


template <typename T> void ioWrite7(uint32_t address, T value)
{
    uint32_t ioAddr = address - 0x4000000;

    // Make sure an I/O register exists at the given address
    if (ioAddr >= sizeof(ioMask7) || !ioMask7[ioAddr])
    {
        printf("Unknown ARM7 I/O write: 0x%X\n", address);
        return;
    }

    // Write data to the ARM7 I/O registers
    // Only bits that are set in the write mask are modified
    *(T*)&ioData7[ioAddr] &= ~(*(T*)&ioWriteMask7[ioAddr]);
    *(T*)&ioData7[ioAddr] |= (value & *(T*)&ioWriteMask7[ioAddr]);

    // Handle special cases
    for (unsigned int i = 0; i < sizeof(T); i++)
    {
        switch (ioAddr + i)
        {
            case 0x0BB: case 0x0C7: case 0x0D3: case 0x0DF: // DMACNT_7
            {
                // Reload the DMA address counters if the enable bit changes from 0 to 1
                if (!(ioData7[ioAddr + i] & BIT(7)) && (((uint8_t*)&value)[i] & BIT(7)))
                {
                    uint8_t channel = (ioAddr + i - 0x0BB) / 0x0C;
                    interpreter::arm7.dmaDstAddrs[channel] = *interpreter::arm7.dmadad[channel];
                    interpreter::arm7.dmaSrcAddrs[channel] = *interpreter::arm7.dmasad[channel];
                }

                // Now that the old enable bit has been used, set the new one
                ioData7[ioAddr + i] &= ~BIT(7);
                ioData7[ioAddr + i] |= (((uint8_t*)&value)[i] & BIT(7));

                break;
            }

            case 0x100: case 0x104: case 0x108: case 0x10C: // TMCNT_L_7
            {
                // Redirect the write to the appropriate timer reload value
                ((uint8_t*)&interpreter::arm7.timerReloads[(ioAddr + i - 0x100) / 4])[0] = ((uint8_t*)&value)[i];
                break;
            }

            case 0x101: case 0x105: case 0x109: case 0x10D: // TMCNT_L_7
            {
                // Redirect the write to the appropriate timer reload value
                ((uint8_t*)&interpreter::arm7.timerReloads[(ioAddr + i - 0x101) / 4])[1] = ((uint8_t*)&value)[i];
                break;
            }

            case 0x102: case 0x106: case 0x10A: case 0x10E: // TMCNT_H_7
            {
                // Reload the appropriate timer counter if the enable bit changes from 0 to 1
                uint8_t timer = (ioAddr + i - 0x102) / 4;
                if (!(ioData7[ioAddr + i] & BIT(7)) && (((uint8_t*)&value)[i] & BIT(7)))
                    *interpreter::arm7.tmcntL[timer] = interpreter::arm7.timerReloads[timer];

                // Now that the old enable bit has been used, set the new one
                ioData7[ioAddr + i] &= ~BIT(7);
                ioData7[ioAddr + i] |= (((uint8_t*)&value)[i] & BIT(7));

                break;
            }

            case 0x138: // RTC
            {
                // Handle writes to the RTC register
                rtc::write(&ioData7[0x138]);
                break;
            }

            case 0x181: // IPCSYNC_7
            {
                // Copy the ARM7 send value to the ARM9 receive value
                ioData9[0x180] = (((uint8_t*)&value)[i] & 0x0F);

                // Trigger a remote IRQ if enabled on both sides
                if ((((uint8_t*)&value)[i] & BIT(5)) && (ioData9[0x181] & BIT(6)))
                    *interpreter::arm9.irf |= BIT(16);

                break;
            }

            case 0x184: // IPCFIFOCNT_7
            {
                // Trigger a send FIFO empty IRQ if the FIFO is empty and the enable bit changes from 0 to 1
                if ((ioData7[0x184] & BIT(0)) && !(ioData7[0x184] & BIT(2)) && (((uint8_t*)&value)[i] & BIT(2)))
                    *interpreter::arm7.irf |= BIT(17); // Send FIFO empty IRQ

                // Now that the old enable bit has been used, set the new one
                ioData7[0x184] &= ~BIT(2);
                ioData7[0x184] |= (((uint8_t*)&value)[i] & BIT(2));

                // Clear the send FIFO if the clear bit is set
                if (((uint8_t*)&value)[i] & BIT(3))
                    fifo::clear(&interpreter::arm7, &interpreter::arm9);

                break;
            }

            case 0x185: // IPCFIFOCNT_7
            {
                // Trigger a receive FIFO not empty IRQ if the FIFO isn't empty and the enable bit changes from 0 to 1
                if (!(ioData7[0x185] & BIT(0)) && !(ioData7[0x185] & BIT(2)) && (((uint8_t*)&value)[i] & BIT(2)))
                    *interpreter::arm7.irf |= BIT(18);

                // Now that the old enable bit has been used, set the new one
                ioData7[0x185] &= ~BIT(2);
                ioData7[0x185] |= (((uint8_t*)&value)[i] & BIT(2));

                // If the error bit is set, acknowledge the error by clearing it
                if (((uint8_t*)&value)[i] & BIT(6))
                    ioData7[0x185] &= ~BIT(6);

                break;
            }

            case 0x188: case 0x189: case 0x18A: case 0x18B: // IPCFIFOSEND_7
            {
                // Trigger a FIFO send, and return so it doesn't trigger multiple times
                fifo::send(&interpreter::arm7, &interpreter::arm9);
                return;
            }

            case 0x1A2: // AUXSPIDATA_7
            {
                // Send the value to the AUX SPI
                spi::auxWrite(&interpreter::arm7, ((uint8_t*)&value)[i]);
                break;
            }

            case 0x1A7: // ROMCTRL_7
            {
                // Set the release reset bit, but never clear it
                ioData7[0x1A7] |= (((uint8_t*)&value)[i] & BIT(5));

                // Save the old start bit for later use and set the new one
                uint8_t startBit = (ioData7[0x1A7] & BIT(7));
                ioData7[0x1A7] &= ~BIT(7);
                ioData7[0x1A7] |= (((uint8_t*)&value)[i] & BIT(7));

                // Start a ROM transfer if the start bit changes from 0 to 1
                if (!startBit && (((uint8_t*)&value)[i] & BIT(7)))
                    cartridge::transferStart(&interpreter::arm7);

                break;
            }

            case 0x1C2: // SPIDATA
            {
                // Send the value to the SPI
                spi::write(((uint8_t*)&value)[i]);
                break;
            }

            case 0x214: case 0x215: case 0x216: case 0x217: // IRF_7
            {
                // Acknowledge interrupts by clearing set bits
                ioData7[ioAddr + i] &= ~((uint8_t*)&value)[i];
                break;
            }

            case 0x300: // POSTFLG_7
            {
                // Set the POSTFLG bit, but never clear it
                ioData7[0x300] |= (((uint8_t*)&value)[i] & BIT(0));
                break;
            }

            case 0x301: // HALTCNT
            {
                // Halt the CPU if halt mode is selected
                // GBA mode and sleep mode can also be selected, but this is enough for now
                if (((((uint8_t*)&value)[i] & 0xC0) >> 6) == 2)
                    interpreter::arm7.halt = true;
                break;
            }
        }
    }
}

template int8_t   read(interpreter::Cpu *cpu, uint32_t address);
template int16_t  read(interpreter::Cpu *cpu, uint32_t address);
template uint8_t  read(interpreter::Cpu *cpu, uint32_t address);
template uint16_t read(interpreter::Cpu *cpu, uint32_t address);
template uint32_t read(interpreter::Cpu *cpu, uint32_t address);
template <typename T> T read(interpreter::Cpu *cpu, uint32_t address)
{
    // Treat the GBA slot as if there's no cart inserted
    if (address >= 0x8000000 && address < 0x9000000)
        return (T)0xFFFFFFFF;

    if (cpu->type == 9)
    {
        if (address >= 0x4000000 && address < 0x5000000)
        {
            // Read from the ARM9 I/O registers
            return ioRead9<T>(address);
        }
        else
        {
            // Read from normal ARM9 memory
            T *src = (T*)memoryMap9(address, true);
            if (src)
                return *src;
            else
                printf("Unmapped ARM9 memory read: 0x%X\n", address);
        }
    }
    else
    {
        if (address >= 0x4000000 && address < 0x5000000)
        {
            // Read from the ARM7 I/O registers
            return ioRead7<T>(address);
        }
        else
        {
            // Read from normal ARM7 memory
            T *src = (T*)memoryMap7(address, true);
            if (src)
                return *src;
            else
                printf("Unmapped ARM7 memory read: 0x%X\n", address);
        }
    }

    return 0;
}

template void write(interpreter::Cpu *cpu, uint32_t address, uint8_t  value);
template void write(interpreter::Cpu *cpu, uint32_t address, uint16_t value);
template void write(interpreter::Cpu *cpu, uint32_t address, uint32_t value);
template <typename T> void write(interpreter::Cpu *cpu, uint32_t address, T value)
{
    // Treat the GBA slot as if there's no cart inserted
    if (address >= 0x8000000 && address < 0x9000000)
        return;

    if (cpu->type == 9)
    {
        if (address >= 0x4000000 && address < 0x5000000)
        {
            // Write to the ARM9 I/O registers
            ioWrite9<T>(address, value);
        }
        else
        {
            // Write to normal ARM9 memory
            T *dst = (T*)memoryMap9(address, false);
            if (dst)
                *dst = value;
            else
                printf("Unmapped ARM9 memory write: 0x%X\n", address);
        }
    }
    else
    {
        if (address >= 0x4000000 && address < 0x5000000)
        {
            // Write to the ARM7 I/O registers
            ioWrite7<T>(address, value);
        }
        else
        {
            // Write to normal ARM7 memory
            T *dst = (T*)memoryMap7(address, false);
            if (dst)
                *dst = value;
            else
                printf("Unmapped ARM7 memory write: 0x%X\n", address);
        }
    }
}

void init()
{
    // Clear memory
    memset(ram,      0, sizeof(ram));
    memset(wram,     0, sizeof(wram));
    memset(instrTcm, 0, sizeof(instrTcm));
    memset(dataTcm,  0, sizeof(dataTcm));
    memset(bios9,    0, sizeof(bios9));
    memset(bios7,    0, sizeof(bios7));
    memset(wram7,    0, sizeof(wram7));

    // Clear VRAM
    memset(palette, 0, sizeof(palette));
    memset(vramA,   0, sizeof(vramA));
    memset(vramB,   0, sizeof(vramB));
    memset(vramC,   0, sizeof(vramC));
    memset(vramD,   0, sizeof(vramD));
    memset(vramE,   0, sizeof(vramE));
    memset(vramF,   0, sizeof(vramF));
    memset(vramG,   0, sizeof(vramG));
    memset(vramH,   0, sizeof(vramH));
    memset(vramI,   0, sizeof(vramI));
    memset(oam,     0, sizeof(oam));

    // Reset memory mappings
    wramOffset9 = wramSize9 = 0;
    wramOffset7 = wramSize7 = 0;
    memset(vramBases,    0, sizeof(vramBases));
    memset(extPalettesA, 0, sizeof(extPalettesA));
    memset(extPalettesB, 0, sizeof(extPalettesB));

    // Clear I/O register data
    memset(ioData9, 0, sizeof(ioData9));
    memset(ioData7, 0, sizeof(ioData7));

    // Set the ARM9 I/O register masks
    // The normal mask indicates which bits exist, and the write mask indicates which bits are writable
    // More info about what each register does (and about the DS in general) can be found at https://problemkaputt.de/gbatek.htm
    *(uint32_t*)&ioMask9[0x000]  = 0xFFFFFFFF; *(uint32_t*)&ioWriteMask9[0x000]  = 0xFFFFFFFF; // DISPCNT_A
    *(uint16_t*)&ioMask9[0x004]  =     0xFFBF; *(uint16_t*)&ioWriteMask9[0x004]  =     0xFFB8; // DISPSTAT_9
    *(uint16_t*)&ioMask9[0x006]  =     0x01FF; *(uint16_t*)&ioWriteMask9[0x006]  =     0x0000; // VCOUNT_9
    *(uint16_t*)&ioMask9[0x008]  =     0xFFFF; *(uint16_t*)&ioWriteMask9[0x008]  =     0xFFFF; // BG0CNT_A
    *(uint16_t*)&ioMask9[0x00A]  =     0xFFFF; *(uint16_t*)&ioWriteMask9[0x00A]  =     0xFFFF; // BG1CNT_A
    *(uint16_t*)&ioMask9[0x00C]  =     0xFFFF; *(uint16_t*)&ioWriteMask9[0x00C]  =     0xFFFF; // BG2CNT_A
    *(uint16_t*)&ioMask9[0x00E]  =     0xFFFF; *(uint16_t*)&ioWriteMask9[0x00E]  =     0xFFFF; // BG3CNT_A
    *(uint16_t*)&ioMask9[0x010]  =     0x01FF; *(uint16_t*)&ioWriteMask9[0x010]  =     0x01FF; // BG0HOFS_A
    *(uint16_t*)&ioMask9[0x012]  =     0x01FF; *(uint16_t*)&ioWriteMask9[0x012]  =     0x01FF; // BG0VOFS_A
    *(uint16_t*)&ioMask9[0x014]  =     0x01FF; *(uint16_t*)&ioWriteMask9[0x014]  =     0x01FF; // BG1HOFS_A
    *(uint16_t*)&ioMask9[0x016]  =     0x01FF; *(uint16_t*)&ioWriteMask9[0x016]  =     0x01FF; // BG1VOFS_A
    *(uint16_t*)&ioMask9[0x018]  =     0x01FF; *(uint16_t*)&ioWriteMask9[0x018]  =     0x01FF; // BG2HOFS_A
    *(uint16_t*)&ioMask9[0x01A]  =     0x01FF; *(uint16_t*)&ioWriteMask9[0x01A]  =     0x01FF; // BG2VOFS_A
    *(uint16_t*)&ioMask9[0x01C]  =     0x01FF; *(uint16_t*)&ioWriteMask9[0x01C]  =     0x01FF; // BG3HOFS_A
    *(uint16_t*)&ioMask9[0x01E]  =     0x01FF; *(uint16_t*)&ioWriteMask9[0x01E]  =     0x01FF; // BG3VOFS_A
    *(uint32_t*)&ioMask9[0x0B0]  = 0x0FFFFFFF; *(uint32_t*)&ioWriteMask9[0x0B0]  = 0x0FFFFFFF; // DMA0SAD_9
    *(uint32_t*)&ioMask9[0x0B4]  = 0x0FFFFFFF; *(uint32_t*)&ioWriteMask9[0x0B4]  = 0x0FFFFFFF; // DMA0DAD_9
    *(uint32_t*)&ioMask9[0x0B8]  = 0xFFFFFFFF; *(uint32_t*)&ioWriteMask9[0x0B8]  = 0x7FFFFFFF; // DMA0CNT_9
    *(uint32_t*)&ioMask9[0x0BC]  = 0x0FFFFFFF; *(uint32_t*)&ioWriteMask9[0x0BC]  = 0x0FFFFFFF; // DMA1SAD_9
    *(uint32_t*)&ioMask9[0x0C0]  = 0x0FFFFFFF; *(uint32_t*)&ioWriteMask9[0x0C0]  = 0x0FFFFFFF; // DMA1DAD_9
    *(uint32_t*)&ioMask9[0x0C4]  = 0xFFFFFFFF; *(uint32_t*)&ioWriteMask9[0x0C4]  = 0x7FFFFFFF; // DMA1CNT_9
    *(uint32_t*)&ioMask9[0x0C8]  = 0x0FFFFFFF; *(uint32_t*)&ioWriteMask9[0x0C8]  = 0x0FFFFFFF; // DMA2SAD_9
    *(uint32_t*)&ioMask9[0x0CC]  = 0x0FFFFFFF; *(uint32_t*)&ioWriteMask9[0x0CC]  = 0x0FFFFFFF; // DMA2DAD_9
    *(uint32_t*)&ioMask9[0x0D0]  = 0xFFFFFFFF; *(uint32_t*)&ioWriteMask9[0x0D0]  = 0x7FFFFFFF; // DMA2CNT_9
    *(uint32_t*)&ioMask9[0x0D4]  = 0x0FFFFFFF; *(uint32_t*)&ioWriteMask9[0x0D4]  = 0x0FFFFFFF; // DMA3SAD_9
    *(uint32_t*)&ioMask9[0x0D8]  = 0x0FFFFFFF; *(uint32_t*)&ioWriteMask9[0x0D8]  = 0x0FFFFFFF; // DMA3DAD_9
    *(uint32_t*)&ioMask9[0x0DC]  = 0xFFFFFFFF; *(uint32_t*)&ioWriteMask9[0x0DC]  = 0x7FFFFFFF; // DMA3CNT_9
    *(uint32_t*)&ioMask9[0x0E0]  = 0xFFFFFFFF; *(uint32_t*)&ioWriteMask9[0x0E0]  = 0xFFFFFFFF; // DMA0FILL
    *(uint32_t*)&ioMask9[0x0E4]  = 0xFFFFFFFF; *(uint32_t*)&ioWriteMask9[0x0E4]  = 0xFFFFFFFF; // DMA1FILL
    *(uint32_t*)&ioMask9[0x0E8]  = 0xFFFFFFFF; *(uint32_t*)&ioWriteMask9[0x0E8]  = 0xFFFFFFFF; // DMA2FILL
    *(uint32_t*)&ioMask9[0x0EC]  = 0xFFFFFFFF; *(uint32_t*)&ioWriteMask9[0x0EC]  = 0xFFFFFFFF; // DMA3FILL
    *(uint16_t*)&ioMask9[0x100]  =     0xFFFF; *(uint16_t*)&ioWriteMask9[0x100]  =     0x0000; // TM0CNT_L_9
    *(uint16_t*)&ioMask9[0x102]  =     0x00C7; *(uint16_t*)&ioWriteMask9[0x102]  =     0x0047; // TM0CNT_H_9
    *(uint16_t*)&ioMask9[0x104]  =     0xFFFF; *(uint16_t*)&ioWriteMask9[0x104]  =     0x0000; // TM1COUNT_9
    *(uint16_t*)&ioMask9[0x106]  =     0x00C7; *(uint16_t*)&ioWriteMask9[0x106]  =     0x0047; // TM1CNT_9
    *(uint16_t*)&ioMask9[0x108]  =     0xFFFF; *(uint16_t*)&ioWriteMask9[0x108]  =     0x0000; // TM2COUNT_9
    *(uint16_t*)&ioMask9[0x10A]  =     0x00C7; *(uint16_t*)&ioWriteMask9[0x10A]  =     0x0047; // TM2CNT_9
    *(uint16_t*)&ioMask9[0x10C]  =     0xFFFF; *(uint16_t*)&ioWriteMask9[0x10C]  =     0x0000; // TM3COUNT_9
    *(uint16_t*)&ioMask9[0x10E]  =     0x00C7; *(uint16_t*)&ioWriteMask9[0x10E]  =     0x0047; // TM3CNT_9
    *(uint16_t*)&ioMask9[0x130]  =     0x03FF; *(uint16_t*)&ioWriteMask9[0x130]  =     0x0000; // KEYINPUT_9
    *(uint16_t*)&ioMask9[0x180]  =     0x6F0F; *(uint16_t*)&ioWriteMask9[0x180]  =     0x4F00; // IPCSYNC_9
    *(uint16_t*)&ioMask9[0x184]  =     0xC70F; *(uint16_t*)&ioWriteMask9[0x184]  =     0x8000; // IPCFIFOCNT_9
    *(uint32_t*)&ioMask9[0x188]  = 0xFFFFFFFF; *(uint32_t*)&ioWriteMask9[0x188]  = 0xFFFFFFFF; // IPCFIFOSEND_9
    *(uint16_t*)&ioMask9[0x1A0]  =     0xE0C3; *(uint16_t*)&ioWriteMask9[0x1A0]  =     0xE043; // AUXSPICNT_9
    *(uint16_t*)&ioMask9[0x1A2]  =     0x00FF; *(uint16_t*)&ioWriteMask9[0x1A2]  =     0x0000; // AUXSPIDATA_9
    *(uint32_t*)&ioMask9[0x1A4]  = 0xFFFFFFFF; *(uint32_t*)&ioWriteMask9[0x1A4]  = 0x5F7F7FFF; // ROMCTRL_9
    *(uint32_t*)&ioMask9[0x1A8]  = 0xFFFFFFFF; *(uint32_t*)&ioWriteMask9[0x1A8]  = 0xFFFFFFFF; // ROMCMDOUT_9
    *(uint32_t*)&ioMask9[0x1AC]  = 0xFFFFFFFF; *(uint32_t*)&ioWriteMask9[0x1AC]  = 0xFFFFFFFF; // ROMCMDOUT_9
    *(uint16_t*)&ioMask9[0x208]  =     0x0001; *(uint16_t*)&ioWriteMask9[0x208]  =     0x0001; // IME_9
    *(uint32_t*)&ioMask9[0x210]  = 0x003F3F7F; *(uint32_t*)&ioWriteMask9[0x210]  = 0x003F3F7F; // IE_9
    *(uint32_t*)&ioMask9[0x214]  = 0x003F3F7F; *(uint32_t*)&ioWriteMask9[0x214]  = 0x00000000; // IRF_9
                 ioMask9[0x240]  =       0x9B;              ioWriteMask9[0x240]  =       0x00; // VRAMCNT_A
                 ioMask9[0x241]  =       0x9B;              ioWriteMask9[0x241]  =       0x00; // VRAMCNT_B
                 ioMask9[0x242]  =       0x9F;              ioWriteMask9[0x242]  =       0x00; // VRAMCNT_C
                 ioMask9[0x243]  =       0x9F;              ioWriteMask9[0x243]  =       0x00; // VRAMCNT_D
                 ioMask9[0x244]  =       0x87;              ioWriteMask9[0x244]  =       0x00; // VRAMCNT_E
                 ioMask9[0x245]  =       0x9F;              ioWriteMask9[0x245]  =       0x00; // VRAMCNT_F
                 ioMask9[0x246]  =       0x9F;              ioWriteMask9[0x246]  =       0x00; // VRAMCNT_G
                 ioMask9[0x247]  =       0x03;              ioWriteMask9[0x247]  =       0x03; // WRAMCNT
                 ioMask9[0x248]  =       0x83;              ioWriteMask9[0x248]  =       0x00; // VRAMCNT_H
                 ioMask9[0x249]  =       0x83;              ioWriteMask9[0x249]  =       0x00; // VRAMCNT_I
    *(uint16_t*)&ioMask9[0x280]  =     0xC003; *(uint16_t*)&ioWriteMask9[0x280]  =     0x0003; // DIVCNT
    *(uint32_t*)&ioMask9[0x290]  = 0xFFFFFFFF; *(uint32_t*)&ioWriteMask9[0x290]  = 0xFFFFFFFF; // DIVNUMER
    *(uint32_t*)&ioMask9[0x294]  = 0xFFFFFFFF; *(uint32_t*)&ioWriteMask9[0x294]  = 0xFFFFFFFF; // DIVNUMER
    *(uint32_t*)&ioMask9[0x298]  = 0xFFFFFFFF; *(uint32_t*)&ioWriteMask9[0x298]  = 0xFFFFFFFF; // DIVDENOM
    *(uint32_t*)&ioMask9[0x29C]  = 0xFFFFFFFF; *(uint32_t*)&ioWriteMask9[0x29C]  = 0xFFFFFFFF; // DIVDENOM
    *(uint32_t*)&ioMask9[0x2A0]  = 0xFFFFFFFF; *(uint32_t*)&ioWriteMask9[0x2A0]  = 0x00000000; // DIVRESULT
    *(uint32_t*)&ioMask9[0x2A4]  = 0xFFFFFFFF; *(uint32_t*)&ioWriteMask9[0x2A4]  = 0x00000000; // DIVRESULT
    *(uint32_t*)&ioMask9[0x2A8]  = 0xFFFFFFFF; *(uint32_t*)&ioWriteMask9[0x2A8]  = 0x00000000; // DIVREMAIN
    *(uint32_t*)&ioMask9[0x2AC]  = 0xFFFFFFFF; *(uint32_t*)&ioWriteMask9[0x2AC]  = 0x00000000; // DIVREMAIN
    *(uint16_t*)&ioMask9[0x2B0]  =     0x8001; *(uint16_t*)&ioWriteMask9[0x2B0]  =     0x0001; // SQRTCNT
    *(uint32_t*)&ioMask9[0x2B4]  = 0xFFFFFFFF; *(uint32_t*)&ioWriteMask9[0x2B4]  = 0x00000000; // SQRTRESULT
    *(uint32_t*)&ioMask9[0x2B8]  = 0xFFFFFFFF; *(uint32_t*)&ioWriteMask9[0x2B8]  = 0xFFFFFFFF; // SQRTPARAM
    *(uint32_t*)&ioMask9[0x2BC]  = 0xFFFFFFFF; *(uint32_t*)&ioWriteMask9[0x2BC]  = 0xFFFFFFFF; // SQRTPARAM
                 ioMask9[0x300]  =       0x03;              ioWriteMask9[0x300]  =       0x02; // POSTFLG_9
    *(uint16_t*)&ioMask9[0x304]  =     0x820F; *(uint16_t*)&ioWriteMask9[0x304]  =     0x820F; // POWCNT1
    *(uint32_t*)&ioMask9[0x1000] = 0xC0B1FFF7; *(uint32_t*)&ioWriteMask9[0x1000] = 0xC0B1FFF7; // DISPCNT_B
    *(uint16_t*)&ioMask9[0x1008] =     0xFFFF; *(uint16_t*)&ioWriteMask9[0x1008] =     0xFFFF; // BG0CNT_B
    *(uint16_t*)&ioMask9[0x100A] =     0xFFFF; *(uint16_t*)&ioWriteMask9[0x100A] =     0xFFFF; // BG1CNT_B
    *(uint16_t*)&ioMask9[0x100C] =     0xFFFF; *(uint16_t*)&ioWriteMask9[0x100C] =     0xFFFF; // BG2CNT_B
    *(uint16_t*)&ioMask9[0x100E] =     0xFFFF; *(uint16_t*)&ioWriteMask9[0x100E] =     0xFFFF; // BG3CNT_B
    *(uint16_t*)&ioMask9[0x1010] =     0x01FF; *(uint16_t*)&ioWriteMask9[0x1010] =     0x01FF; // BG0HOFS_B
    *(uint16_t*)&ioMask9[0x1012] =     0x01FF; *(uint16_t*)&ioWriteMask9[0x1012] =     0x01FF; // BG0VOFS_B
    *(uint16_t*)&ioMask9[0x1014] =     0x01FF; *(uint16_t*)&ioWriteMask9[0x1014] =     0x01FF; // BG1HOFS_B
    *(uint16_t*)&ioMask9[0x1016] =     0x01FF; *(uint16_t*)&ioWriteMask9[0x1016] =     0x01FF; // BG1VOFS_B
    *(uint16_t*)&ioMask9[0x1018] =     0x01FF; *(uint16_t*)&ioWriteMask9[0x1018] =     0x01FF; // BG2HOFS_B
    *(uint16_t*)&ioMask9[0x101A] =     0x01FF; *(uint16_t*)&ioWriteMask9[0x101A] =     0x01FF; // BG2VOFS_B
    *(uint16_t*)&ioMask9[0x101C] =     0x01FF; *(uint16_t*)&ioWriteMask9[0x101C] =     0x01FF; // BG3HOFS_B
    *(uint16_t*)&ioMask9[0x101E] =     0x01FF; *(uint16_t*)&ioWriteMask9[0x101E] =     0x01FF; // BG3VOFS_B

    // Set the ARM7 I/O register masks
    // The normal mask indicates which bits exist, and the write mask indicates which bits are writable
    // More info about what each register does (and about the DS in general) can be found at https://problemkaputt.de/gbatek.htm
    *(uint16_t*)&ioMask7[0x004] =     0xFFBF; *(uint16_t*)&ioWriteMask7[0x004] =     0xFFB8; // DISPSTAT_7
    *(uint16_t*)&ioMask7[0x006] =     0x01FF; *(uint16_t*)&ioWriteMask7[0x006] =     0x0000; // VCOUNT_7
    *(uint32_t*)&ioMask7[0x0B0] = 0x07FFFFFF; *(uint32_t*)&ioWriteMask7[0x0B0] = 0x07FFFFFF; // DMA0SAD_7
    *(uint32_t*)&ioMask7[0x0B4] = 0x07FFFFFF; *(uint32_t*)&ioWriteMask7[0x0B4] = 0x07FFFFFF; // DMA0DAD_7
    *(uint32_t*)&ioMask7[0x0B8] = 0xF7E03FFF; *(uint32_t*)&ioWriteMask7[0x0B8] = 0x77E03FFF; // DMA0CNT_7
    *(uint32_t*)&ioMask7[0x0BC] = 0x07FFFFFF; *(uint32_t*)&ioWriteMask7[0x0BC] = 0x07FFFFFF; // DMA1SAD_7
    *(uint32_t*)&ioMask7[0x0C0] = 0x07FFFFFF; *(uint32_t*)&ioWriteMask7[0x0C0] = 0x07FFFFFF; // DMA1DAD_7
    *(uint32_t*)&ioMask7[0x0C4] = 0xF7E03FFF; *(uint32_t*)&ioWriteMask7[0x0C4] = 0x77E03FFF; // DMA1CNT_7
    *(uint32_t*)&ioMask7[0x0C8] = 0x0FFFFFFF; *(uint32_t*)&ioWriteMask7[0x0C8] = 0x07FFFFFF; // DMA2SAD_7
    *(uint32_t*)&ioMask7[0x0CC] = 0x0FFFFFFF; *(uint32_t*)&ioWriteMask7[0x0CC] = 0x07FFFFFF; // DMA2DAD_7
    *(uint32_t*)&ioMask7[0x0D0] = 0xF7E03FFF; *(uint32_t*)&ioWriteMask7[0x0D0] = 0x77E03FFF; // DMA2CNT_7
    *(uint32_t*)&ioMask7[0x0D4] = 0x07FFFFFF; *(uint32_t*)&ioWriteMask7[0x0D4] = 0x07FFFFFF; // DMA3SAD_7
    *(uint32_t*)&ioMask7[0x0D8] = 0x07FFFFFF; *(uint32_t*)&ioWriteMask7[0x0D8] = 0x07FFFFFF; // DMA3DAD_7
    *(uint32_t*)&ioMask7[0x0DC] = 0xF7E0FFFF; *(uint32_t*)&ioWriteMask7[0x0DC] = 0x77E0FFFF; // DMA3CNT_7
    *(uint16_t*)&ioMask7[0x100] =     0xFFFF; *(uint16_t*)&ioWriteMask7[0x100] =     0x0000; // TM0CNT_L_7
    *(uint16_t*)&ioMask7[0x102] =     0x00C7; *(uint16_t*)&ioWriteMask7[0x102] =     0x0047; // TM0CNT_H_7
    *(uint16_t*)&ioMask7[0x104] =     0xFFFF; *(uint16_t*)&ioWriteMask7[0x104] =     0x0000; // TM1COUNT_7
    *(uint16_t*)&ioMask7[0x106] =     0x00C7; *(uint16_t*)&ioWriteMask7[0x106] =     0x0047; // TM1CNT_7
    *(uint16_t*)&ioMask7[0x108] =     0xFFFF; *(uint16_t*)&ioWriteMask7[0x108] =     0x0000; // TM2COUNT_7
    *(uint16_t*)&ioMask7[0x10A] =     0x00C7; *(uint16_t*)&ioWriteMask7[0x10A] =     0x0047; // TM2CNT_7
    *(uint16_t*)&ioMask7[0x10C] =     0xFFFF; *(uint16_t*)&ioWriteMask7[0x10C] =     0x0000; // TM3COUNT_7
    *(uint16_t*)&ioMask7[0x10E] =     0x00C7; *(uint16_t*)&ioWriteMask7[0x10E] =     0x0047; // TM3CNT_7
    *(uint16_t*)&ioMask7[0x130] =     0x03FF; *(uint16_t*)&ioWriteMask7[0x130] =     0x0000; // KEYINPUT_7
    *(uint16_t*)&ioMask7[0x136] =     0x00FF; *(uint16_t*)&ioWriteMask7[0x136] =     0x0000; // EXTKEYIN
    *(uint16_t*)&ioMask7[0x138] =     0xFFFF; *(uint16_t*)&ioWriteMask7[0x138] =     0xFFFF; // RTC
    *(uint16_t*)&ioMask7[0x180] =     0x6F0F; *(uint16_t*)&ioWriteMask7[0x180] =     0x4F00; // IPCSYNC_7
    *(uint16_t*)&ioMask7[0x184] =     0xC70F; *(uint16_t*)&ioWriteMask7[0x184] =     0x8000; // IPCFIFOCNT_7
    *(uint32_t*)&ioMask7[0x188] = 0xFFFFFFFF; *(uint32_t*)&ioWriteMask7[0x188] = 0xFFFFFFFF; // IPCFIFOSEND_7
    *(uint16_t*)&ioMask7[0x1A0] =     0xE0C3; *(uint16_t*)&ioWriteMask7[0x1A0] =     0xE043; // AUXSPICNT_7
    *(uint16_t*)&ioMask7[0x1A2] =     0x00FF; *(uint16_t*)&ioWriteMask7[0x1A2] =     0x0000; // AUXSPIDATA_7
    *(uint32_t*)&ioMask7[0x1A4] = 0xFFFFFFFF; *(uint32_t*)&ioWriteMask7[0x1A4] = 0x5F7F7FFF; // ROMCTRL_7
    *(uint32_t*)&ioMask7[0x1A8] = 0xFFFFFFFF; *(uint32_t*)&ioWriteMask7[0x1A8] = 0xFFFFFFFF; // ROMCMDOUT_7
    *(uint32_t*)&ioMask7[0x1AC] = 0xFFFFFFFF; *(uint32_t*)&ioWriteMask7[0x1AC] = 0xFFFFFFFF; // ROMCMDOUT_7
    *(uint16_t*)&ioMask7[0x1C0] =     0xCF83; *(uint16_t*)&ioWriteMask7[0x1C0] =     0xCF03; // SPICNT
    *(uint16_t*)&ioMask7[0x1C2] =     0x00FF; *(uint16_t*)&ioWriteMask7[0x1C2] =     0x0000; // SPIDATA
    *(uint16_t*)&ioMask7[0x208] =     0x0001; *(uint16_t*)&ioWriteMask7[0x208] =     0x0001; // IME_7
    *(uint32_t*)&ioMask7[0x210] = 0x01FF3FFF; *(uint32_t*)&ioWriteMask7[0x210] = 0x01FF3FFF; // IE_7
    *(uint32_t*)&ioMask7[0x214] = 0x01FF3FFF; *(uint32_t*)&ioWriteMask7[0x214] = 0x00000000; // IRF_7
                 ioMask7[0x241] =       0x03;              ioWriteMask7[0x241] =       0x00; // WRAMSTAT
                 ioMask7[0x300] =       0x01;              ioWriteMask7[0x300] =       0x00; // POSTFLG_7
                 ioMask7[0x301] =       0xC0;              ioWriteMask7[0x301] =       0xC0; // HALTCNT
    *(uint16_t*)&ioMask7[0x504] =     0x03FF; *(uint16_t*)&ioWriteMask7[0x504] =     0x03FF; // SOUNDBIAS

    // Set pointers to the data of registers used by the ARM9
    interpreter::arm9.dispstat    = (uint16_t*)&ioData9[0x004];
    interpreter::arm9.vcount      = (uint16_t*)&ioData9[0x006];
    interpreter::arm9.dmasad[0]   = (uint32_t*)&ioData9[0x0B0];
    interpreter::arm9.dmadad[0]   = (uint32_t*)&ioData9[0x0B4];
    interpreter::arm9.dmacnt[0]   = (uint32_t*)&ioData9[0x0B8];
    interpreter::arm9.dmasad[1]   = (uint32_t*)&ioData9[0x0BC];
    interpreter::arm9.dmadad[1]   = (uint32_t*)&ioData9[0x0C0];
    interpreter::arm9.dmacnt[1]   = (uint32_t*)&ioData9[0x0C4];
    interpreter::arm9.dmasad[2]   = (uint32_t*)&ioData9[0x0C8];
    interpreter::arm9.dmadad[2]   = (uint32_t*)&ioData9[0x0CC];
    interpreter::arm9.dmacnt[2]   = (uint32_t*)&ioData9[0x0D0];
    interpreter::arm9.dmasad[3]   = (uint32_t*)&ioData9[0x0D4];
    interpreter::arm9.dmadad[3]   = (uint32_t*)&ioData9[0x0D8];
    interpreter::arm9.dmacnt[3]   = (uint32_t*)&ioData9[0x0DC];
    interpreter::arm9.tmcntL[0]   = (uint16_t*)&ioData9[0x100];
    interpreter::arm9.tmcntH[0]   = (uint16_t*)&ioData9[0x102];
    interpreter::arm9.tmcntL[1]   = (uint16_t*)&ioData9[0x104];
    interpreter::arm9.tmcntH[1]   = (uint16_t*)&ioData9[0x106];
    interpreter::arm9.tmcntL[2]   = (uint16_t*)&ioData9[0x108];
    interpreter::arm9.tmcntH[2]   = (uint16_t*)&ioData9[0x10A];
    interpreter::arm9.tmcntL[3]   = (uint16_t*)&ioData9[0x10C];
    interpreter::arm9.tmcntH[3]   = (uint16_t*)&ioData9[0x10E];
    interpreter::arm9.keyinput    = (uint16_t*)&ioData9[0x130];
    interpreter::arm9.ipcfifocnt  = (uint16_t*)&ioData9[0x184];
    interpreter::arm9.ipcfifosend = (uint32_t*)&ioData9[0x188];
    interpreter::arm9.auxspicnt   = (uint16_t*)&ioData9[0x1A0];
    interpreter::arm9.auxspidata  = (uint16_t*)&ioData9[0x1A2];
    interpreter::arm9.romctrl     = (uint32_t*)&ioData9[0x1A4];
    interpreter::arm9.romcmdout   = (uint64_t*)&ioData9[0x1A8];
    interpreter::arm9.ime         = (uint16_t*)&ioData9[0x208];
    interpreter::arm9.ie          = (uint32_t*)&ioData9[0x210];
    interpreter::arm9.irf         = (uint32_t*)&ioData9[0x214];

    // Set pointers to the data of registers used by the ARM7
    interpreter::arm7.dispstat    = (uint16_t*)&ioData7[0x004];
    interpreter::arm7.vcount      = (uint16_t*)&ioData7[0x006];
    interpreter::arm7.dmasad[0]   = (uint32_t*)&ioData7[0x0B0];
    interpreter::arm7.dmadad[0]   = (uint32_t*)&ioData7[0x0B4];
    interpreter::arm7.dmacnt[0]   = (uint32_t*)&ioData7[0x0B8];
    interpreter::arm7.dmasad[1]   = (uint32_t*)&ioData7[0x0BC];
    interpreter::arm7.dmadad[1]   = (uint32_t*)&ioData7[0x0C0];
    interpreter::arm7.dmacnt[1]   = (uint32_t*)&ioData7[0x0C4];
    interpreter::arm7.dmasad[2]   = (uint32_t*)&ioData7[0x0C8];
    interpreter::arm7.dmadad[2]   = (uint32_t*)&ioData7[0x0CC];
    interpreter::arm7.dmacnt[2]   = (uint32_t*)&ioData7[0x0D0];
    interpreter::arm7.dmasad[3]   = (uint32_t*)&ioData7[0x0D4];
    interpreter::arm7.dmadad[3]   = (uint32_t*)&ioData7[0x0D8];
    interpreter::arm7.dmacnt[3]   = (uint32_t*)&ioData7[0x0DC];
    interpreter::arm7.tmcntL[0]   = (uint16_t*)&ioData7[0x100];
    interpreter::arm7.tmcntH[0]   = (uint16_t*)&ioData7[0x102];
    interpreter::arm7.tmcntL[1]   = (uint16_t*)&ioData7[0x104];
    interpreter::arm7.tmcntH[1]   = (uint16_t*)&ioData7[0x106];
    interpreter::arm7.tmcntL[2]   = (uint16_t*)&ioData7[0x108];
    interpreter::arm7.tmcntH[2]   = (uint16_t*)&ioData7[0x10A];
    interpreter::arm7.tmcntL[3]   = (uint16_t*)&ioData7[0x10C];
    interpreter::arm7.tmcntH[3]   = (uint16_t*)&ioData7[0x10E];
    interpreter::arm7.keyinput    = (uint16_t*)&ioData7[0x130];
    interpreter::arm7.ipcfifocnt  = (uint16_t*)&ioData7[0x184];
    interpreter::arm7.ipcfifosend = (uint32_t*)&ioData7[0x188];
    interpreter::arm7.auxspicnt   = (uint16_t*)&ioData7[0x1A0];
    interpreter::arm7.auxspidata  = (uint16_t*)&ioData7[0x1A2];
    interpreter::arm7.romctrl     = (uint32_t*)&ioData7[0x1A4];
    interpreter::arm7.romcmdout   = (uint64_t*)&ioData7[0x1A8];
    interpreter::arm7.ime         = (uint16_t*)&ioData7[0x208];
    interpreter::arm7.ie          = (uint32_t*)&ioData7[0x210];
    interpreter::arm7.irf         = (uint32_t*)&ioData7[0x214];

    // Set pointers to the data of registers used by GPU engine A
    gpu::engineA.dispcnt   = (uint32_t*)&ioData9[0x000];
    gpu::engineA.bgcnt[0]  = (uint16_t*)&ioData9[0x008];
    gpu::engineA.bgcnt[1]  = (uint16_t*)&ioData9[0x00A];
    gpu::engineA.bgcnt[2]  = (uint16_t*)&ioData9[0x00C];
    gpu::engineA.bgcnt[3]  = (uint16_t*)&ioData9[0x00E];
    gpu::engineA.bghofs[0] = (uint16_t*)&ioData9[0x010];
    gpu::engineA.bgvofs[0] = (uint16_t*)&ioData9[0x012];
    gpu::engineA.bghofs[1] = (uint16_t*)&ioData9[0x014];
    gpu::engineA.bgvofs[1] = (uint16_t*)&ioData9[0x016];
    gpu::engineA.bghofs[2] = (uint16_t*)&ioData9[0x018];
    gpu::engineA.bgvofs[2] = (uint16_t*)&ioData9[0x01A];
    gpu::engineA.bghofs[3] = (uint16_t*)&ioData9[0x01C];
    gpu::engineA.bgvofs[3] = (uint16_t*)&ioData9[0x01E];
    gpu::engineA.palette   = (uint16_t*)palette;
    gpu::engineA.oam       = (uint16_t*)oam;
    gpu::engineA.extPalettes = extPalettesA;
    gpu::engineA.bgVramAddr  = 0x6000000;
    gpu::engineA.objVramAddr = 0x6400000;

    // Set pointers to the data of registers used by GPU engine B
    gpu::engineB.dispcnt   = (uint32_t*)&ioData9[0x1000];
    gpu::engineB.bgcnt[0]  = (uint16_t*)&ioData9[0x1008];
    gpu::engineB.bgcnt[1]  = (uint16_t*)&ioData9[0x100A];
    gpu::engineB.bgcnt[2]  = (uint16_t*)&ioData9[0x100C];
    gpu::engineB.bgcnt[3]  = (uint16_t*)&ioData9[0x100E];
    gpu::engineB.bghofs[0] = (uint16_t*)&ioData9[0x1010];
    gpu::engineB.bgvofs[0] = (uint16_t*)&ioData9[0x1012];
    gpu::engineB.bghofs[1] = (uint16_t*)&ioData9[0x1014];
    gpu::engineB.bgvofs[1] = (uint16_t*)&ioData9[0x1016];
    gpu::engineB.bghofs[2] = (uint16_t*)&ioData9[0x1018];
    gpu::engineB.bgvofs[2] = (uint16_t*)&ioData9[0x101A];
    gpu::engineB.bghofs[3] = (uint16_t*)&ioData9[0x101C];
    gpu::engineB.bgvofs[3] = (uint16_t*)&ioData9[0x101E];
    gpu::engineB.palette   = (uint16_t*)&palette[0x400];
    gpu::engineB.oam       = (uint16_t*)&oam[0x400];
    gpu::engineB.extPalettes = extPalettesB;
    gpu::engineB.bgVramAddr  = 0x6200000;
    gpu::engineB.objVramAddr = 0x6600000;

    // Set key bits to indicate the keys are released
    *interpreter::arm9.keyinput = 0x03FF;
    *interpreter::arm7.keyinput = 0x03FF;
    *extkeyin = 0x007F;

    // Set FIFO empty bits
    *interpreter::arm9.ipcfifocnt = 0x0101;
    *interpreter::arm7.ipcfifocnt = 0x0101;
}

}
