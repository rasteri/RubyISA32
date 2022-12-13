#include <pc.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define uint32_t unsigned long

void listports(uint32_t BASE, uint32_t MASK)
{
    uint32_t cnt;
    uint32_t lastOne = 0;
    uint32_t thisOne = 0;
    //each bit in the mask is ignored in the address
    MASK |= 0x03;
    for (cnt = 0; cnt < 0xFFFF; cnt++)
    {
        if ((cnt | MASK) == (BASE | MASK))
        {
            // starting new run, print first
            if (lastOne == 0){
                printf("%X-", cnt);
                lastOne = cnt;
            }
            else if (cnt == lastOne + 1)
            {
                // don't print consecutive ports
                lastOne = cnt;
            }
            else // we've broken a sequence, print the last one and start a new range
            {
                printf("%X,", lastOne);
                printf("%X-", cnt);
                lastOne = cnt;
            }
        }
    }
    printf("%X", lastOne);
    printf("\n");
    printf("\n");
}

void dec(uint32_t val)
{
    uint32_t BASE = (val & 0xFFFC);
    uint32_t MASK = (val & 0xFC0000) >> 16;
    printf("BASE : %lx\n", BASE);
    printf("MASK : %lx\n", MASK);

    listports(BASE, MASK);
}

uint32_t LPCEnc(uint32_t BASE, uint32_t MASK)
{
    return BASE | (MASK << 16) | 0x01;
}

typedef struct range
{
    // base address, only LS 14 bits are used
    uint32_t base;

    // only MS 6 bits are used
    // any digits that are 1 will be ignored in the decode comparison logic
    uint32_t mask;
} range;

uint32_t LPCGenericDecode[] = {0x8000F884, 0x8000F888, 0x8000F88C, 0x8000F890};

uint32_t ISAGenericDecode[] = {0x20, 0x23, 0x30, 0x33};

range ranges[] = {
    {0x200, 0xFC}, //200-2FF
    {0x300, 0x70}, //300/310/320/330/340/350/360/370+3
    {0x388, 0x1C}, //380-38F, 390-39F
    {0xA00, 0xFC}  //A00-AFF
};

int main(int argc, char *argv[])
{
    uint32_t tmp;
    int numRanges;
    if (argc < 3 || !(argc & 0x01))
    {
        printf("Malformed args, using defaults\n");
        numRanges = 4;
    }
    else
    {
        numRanges = (argc - 1) / 2;
        for (tmp = 0; tmp < numRanges; tmp++)
        {
            ranges[tmp].base = strtoul(argv[(tmp * 2) + 1], NULL, 16);
            ranges[tmp].mask = strtoul(argv[(tmp * 2) + 2], NULL, 16);
        }
    }

    // Query ICH7 LPC controller
    outportl(0xCF8, 0x8000F800);
    tmp = inportl(0xCFC);
    if (tmp != 0x27B88086)
    {
        printf("Can't find ICH7 LPC\n");
        printf("(Got %X, expected %X)\n", tmp, 0x27B88086);
        goto errexit;
    }
    printf("Found Intel ICH7 LPC.\n");

    // Enable ISA bridge config registers
    outportb(0x4E, 0x26);
    outportb(0x4E, 0x26);

    // check ISA bridge ID
    tmp = 0;
    outportb(0x4E, 0x5A);
    tmp |= inportb(0x4F) << 24;
    outportb(0x4E, 0x5B);
    tmp |= inportb(0x4F) << 16;
    outportb(0x4E, 0x5C); //tmp |= inportb(0x4F);
    outportb(0x4E, 0x5D);
    tmp |= inportb(0x4F) << 8;
    outportb(0x4E, 0x5E);
    tmp |= inportb(0x4F);

    if (tmp != 0x03051934)
    {
        printf("Can't find Fintek F85226FG LPC-ISA Bridge\n");
        printf("(Got %X, expected %X)\n", tmp, 0x03051934);
        goto errexit;
    }
    printf("Found Fintek F85226FG LPC-ISA Bridge!\n");

    for (tmp = 0; tmp < numRanges; tmp++)
    {
        printf("Enabling Range %x : Base %x, Mask %x, LPC %x\n", tmp, ranges[tmp].base, ranges[tmp].mask, LPCEnc(ranges[tmp].base, ranges[tmp].mask));
        printf ("Ports : ");
        listports(ranges[tmp].base, ranges[tmp].mask);

        // first forward the port in the ICH7 to LPC
        outportl(0xCF8, LPCGenericDecode[tmp]);
        uint32_t enc = LPCEnc(ranges[tmp].base, ranges[tmp].mask);
        outportb(0xCFC, enc & 0xFF);
        enc >>= 8;
        outportb(0xCFD, enc & 0xFF);
        enc >>= 8;
        outportb(0xCFE, enc & 0xFF);

        // now tell the LPC-ISA Bridge to expect it
        outportb(0x4E, ISAGenericDecode[tmp] + 1);
        outportb(0x4F, ranges[tmp].base & 0xFF);

        outportb(0x4E, ISAGenericDecode[tmp] + 2);
        outportb(0x4F, (ranges[tmp].base >> 8) & 0xFF);

        outportb(0x4E, ISAGenericDecode[tmp]);
        outportb(0x4F, ranges[tmp].mask | 0x03);

        outportl(0xCF8, 0x00);
    }

    // Enable A17/A18/A19
    outportb(0x4E, 0x05);
    outportb(0x4F, 0x0E);

    // 8MHz clock, 3 8bit waitstates, 1 16bit waitstate
    outportb(0x4E, 0x06);
    outportb(0x4F, 0b01011101);
/*    outportb(0x4E, 0x06);
   outportb(0x4F, 0b01111110);*/


    // Output sysclk
    outportb(0x4E, 0x50);
    outportb(0x4F, 0x00);

    // Disable power management
    outportb(0x4E, 0x51);
    outportb(0x4F, 0x00);
    outportb(0x4E, 0x51);
    outportb(0x4F, 0x00);

    return 0;

errexit:
    outportl(0xCF8, 0x00);
    return 1;
}
