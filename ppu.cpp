#include "ppu.h"

ppu::ppu() : writeToggle(false), oddFrame(false), reg2000(0x00), reg2001(0x00), NMI(false), vblank(false), ntFetch(true)
		,ppuAddress(0x0000), scanline(0), dotNumber(0), bufferVblank(false), horizontalDot(8), reloadDot(9)
{	
	ppuDebug.open("ppuDebug.txt");
}

ppu::~ppu()
{
	ppuDebug.close();
}

void ppu::emulateCycle(memory* memory)
{
	//Update the registers
	reg2000 = memory->RAM[0x2000];
	reg2001 = memory->RAM[0x2001];
	ppuAddress %= 0x3FFF;				//Can't be bigger than this

	checkVblank(memory);				//Checks if vblank is happening

	if(!vblank)					//If vblank is going on, none of this happens
	{

		//These are the only pixels that make it onto the screen
		//The top and bottom 8 scanlines are cut off
		if(scanline > 7 && scanline < 232)
		{
			if(dotNumber > 0 && dotNumber < 257) renderPixel(memory);
		}

		if(reg2001 & 0x18)				//Checks if rendering is enabled
		{
			if(scanline < 240 || scanline == 261)
			{
				//Reload the registers
				if(dotNumber == reloadDot)
				{
					//Fills the upper 8 bits with next tiles
					highBGShift |= highBGFetch << 8; 
					lowBGShift |= lowBGFetch << 8; 
					fourToOneMux();							//Fills the attribute registers
					reloadDot += 8;							//Reload up to 257
				}
			}
			
			if(idleCounter != 0) idleCounter--;
			else
			{
				//For getting the background data.
				if(scanline < 240) 
				{
					visableScanline(memory);					//For visable scanlines
				}
				else if(scanline == 240) 
				{
					idleCounter += 340;						//Idles for a scanline + 1 dot
				}
				else 
				{
					preBGFetch(memory);						//For the prerender scanline
				}
			}

			if(scanline < 240 || scanline == 261)
			{
				//The registers only shift between dots 2-257 and 322-337 (inclusive)
				if((dotNumber > 1 && dotNumber < 258) || (dotNumber > 321 && dotNumber < 338)) shiftRegisters();
			}

			checkDotNumber();						//Does stuff depending on the dot number
		}
	}

	if(dotNumber == 340) 
	{
		scanline = (scanline + 1) % 262;		//262 scanlines in a frame
	}

	dotNumber = (dotNumber + 1) % 341;			//341 dots in a scanline
}

void ppu::setIdleCounter(unsigned short value)
{
	idleCounter = value;
}


//----------------------------------------------------------------------------------------------------------------------------------------
//Private functions
const void ppu::checkDotNumber()
{
	if(dotNumber == 256)
	{
		if((ppuAddress & 0x7000) != 0x7000)	ppuAddress += 0x1000;	//If fine Y < 7 increment it
		else
		{
			ppuAddress &= ~0x7000;		//Fine Y = 0
			coarseY = (ppuAddress & 0x03E0) >> 5;	//Let y = coarse Y
			if(coarseY == 29)
			{
				coarseY = 0;			//Coarse Y = 0
				ppuAddress = (ppuAddress & ~0x03E0) | (coarseY << 5);		//Put coarse Y back into address
				ppuAddress ^= 0x8000;		//Switch vertical nametable
			}
			else if(coarseY == 31) 
			{
				coarseY = 0;			//Coarse Y = 0, nametable not switched
				ppuAddress = (ppuAddress & ~0x03E0) | (coarseY << 5);		//Put coarse Y back into address
			}
			else
			{
				coarseY++;							//Increment coarse Y
				ppuAddress = (ppuAddress & ~0x03E0) | (coarseY << 5);		//Put coarse Y back into address
			}
		}
	}


	if(dotNumber == 257)
	{
		ppuAddress &= 0x7DF0;						//Clears the bits for horizontal position
		ppuAddress |= ppuTempAddress & ~0x7DF0;				//Keeps the bits that were cleared above
		horizontalDot = 328;						//Next time this is needed
		reloadDot = 329;						//Next time the registers are reloaded
	}
	else if(dotNumber > 279 && dotNumber < 305)
	{
		ppuAddress &= ~0xFBE0;						//Clears the vertical bits
		ppuAddress |= ppuTempAddress & 0xFBE0;		//Puts the vertical bits in
	}
	else if(dotNumber < 257 || dotNumber > 327)
	{
		if(dotNumber == horizontalDot)
		{
			if((ppuAddress & 0x001F) == 31) 	// if coarse X == 31
			{
				ppuAddress &= ~0x001F;         	// coarse X = 0
				ppuAddress ^= 0x0400;           // switch horizontal nametable
			}
			else ppuAddress++;                	// increment coarse X

			horizontalDot += 8;
		}
	
		if(dotNumber == 336) horizontalDot = 8;				//Next dot to increment horizontal position
		else if(dotNumber == 337) reloadDot = 9;			//Next dot to reload registers
	}
}

const void ppu::shiftRegisters()
{
	//The registers only shift between dots 2-257 and 322-337 (inclusive)
	lowBGShift >>= 1;
	highBGShift >>= 1;
	//lowAtShift >>= 1;
	//highAttShift >>= 1;

}

const void ppu::renderPixel(memory *memory)
{
	if(reg2001 & 0x18)
	{
		if(reg2001 & 0x08)		//Checks if background rendering is enabled
		{
			palleteAddress = 0x3F00 | eightToOneMux1(lowBGShift) | (eightToOneMux1(highBGShift) << 1)
							| (eightToOneMux2(lowAttShift) << 2) | (eightToOneMux2(highAttShift) << 3);
			palleteData = memory->readVRAM(palleteAddress);
		
			//Sets the RGB values for the screen
			screenData[scanline - 8][dotNumber - 1][0] = red[palleteData];
			screenData[scanline - 8][dotNumber - 1][1] = green[palleteData];
			screenData[scanline - 8][dotNumber - 1][2] = blue[palleteData];
		}
		else
		{
			if(ppuAddress >= 0x3F00) palleteAddress = ppuAddress;
			else palleteAddress = 0x3F00;
			palleteData = memory->readVRAM(palleteAddress);

			//Sets the RGB values for the screen
			screenData[scanline - 8][dotNumber - 1][0] = red[palleteData];
			screenData[scanline - 8][dotNumber - 1][1] = green[palleteData];
			screenData[scanline - 8][dotNumber - 1][2] = blue[palleteData];
		}
	}
	else
	{
		if(ppuAddress >= 0x3F00) palleteAddress = ppuAddress;
		else palleteAddress = 0x3F00;
		palleteData = memory->readVRAM(palleteAddress);

		//Sets the RGB values for the screen
		screenData[scanline - 8][dotNumber - 1][0] = red[palleteData];
		screenData[scanline - 8][dotNumber - 1][1] = green[palleteData];
		screenData[scanline - 8][dotNumber - 1][2] = blue[palleteData];
	}
}

const void ppu::checkVblank(memory* memory)
{
	if(scanline == 241 && dotNumber == 1) 
	{
		ppu* dummy = NULL;							//Dummy pointer so I can write to RAM
		memory->writeRAM(reg2002, vblankValue, dummy);
		vblank = true;
		bufferVblank = true;
		if(reg2000 & 0x80) NMI = true;
	}
	else if(scanline == 261 && dotNumber == 1)
	{
		ppu* dummy = NULL;
		unsigned char clearData = 0x9F;				//Value that clears the flags
		zeroFlag = false;
		spriteOverflow = false;
		vblank = false;
		memory->writeRAM(reg2002, clearData, dummy);		//Clears sprite 0 hit and overflow bit
	}
}



//----------------------------------------------------------------------------------------------------------------------------------------
//Scanline functions
const void ppu::visableScanline(memory *memory)
{
	if(dotNumber == 257) idleCounter += 63;		//Idle between cycles 258-320
	else visableBGFetch(memory);
}		

const void ppu::visableBGFetch(memory* memory)
{
	if((dotNumber > 0 && dotNumber < 257) || (dotNumber > 320 && dotNumber < 337))
	{
		if(ntFetch)
		{
			nameAddress = 0x2000 | (ppuAddress & 0x0FFF);
			nameFetch = memory->readVRAM(nameAddress);
			ntFetch = false;
			atFetch = true;
			idleCounter++;
		}
		else if(atFetch)
		{
			attAddress = 0x23C0 | (ppuAddress & 0x0C00) | ((ppuAddress >> 4) & 0x38) | ((ppuAddress >> 2) & 0x07);
			attFetch = memory->VRAM[attAddress];
			atFetch = false;
			bgLowFetch = true;
			idleCounter++;
		}
		else if(bgLowFetch)
		{
			//Finds the bg tile address
			if(reg2000 & 0x10) tileAddress = 0x1000 | (nameFetch << 4) | ((ppuAddress & 0x7000) >> 12);
			else tileAddress = 0x0000 | (nameFetch << 4) | ((ppuAddress & 0x7000) >> 12);
			lowBGFetch = memory->readVRAM(tileAddress);
			//ppuDebug << "Tile: " << std::hex << tileAddress << std::endl;
			bgLowFetch = false;
			idleCounter++;
		}
		else
		{
			tileAddress += 8;			//8 bytes ahead
			highBGFetch = memory->readVRAM(tileAddress);	//8 bytes ahead of the address
			ntFetch = true;
			idleCounter++;
		}	
	}
	else
	{
		nameAddress = 0x2000 | (ppuAddress & 0x0FFF);
		nameFetch = memory->readVRAM(nameAddress);
		idleCounter++;
	}
}


const void ppu::preBGFetch(memory *memory)
{
	if((dotNumber > 0 && dotNumber < 257) || (dotNumber > 320 && dotNumber < 337))
	{
		if(ntFetch)
		{
			nameAddress = 0x2000 | (ppuAddress & 0x0FFF);
			nameFetch = memory->readVRAM(nameAddress);
			ntFetch = false;
			atFetch = true;
			idleCounter++;
		}
		else if(atFetch)
		{
			attAddress = 0x23C0 | (ppuAddress & 0x0C00) | ((ppuAddress >> 4) & 0x38) | ((ppuAddress >> 2) & 0x07);
			attFetch = memory->VRAM[attAddress];
			atFetch = false;
			bgLowFetch = true;
			idleCounter++;
		}
		else if(bgLowFetch)
		{
			//Finds the bg tile address
			if(reg2000 & 0x10) tileAddress = 0x1000 | (nameFetch << 4) | ((ppuAddress & 0x7000) >> 12);
			else tileAddress = 0x0000 | (nameFetch << 4) | ((ppuAddress & 0x7000) >> 12);
			lowBGFetch = memory->readVRAM(tileAddress);
			bgLowFetch = false;
			idleCounter++;
		}
		else
		{
			highBGFetch = memory->VRAM[tileAddress + 8];	//8 bytes ahead of the address
			ntFetch = true;
			idleCounter++;
		}	
	}
	else
	{
		nameAddress = 0x2000 | (ppuAddress & 0x0FFF);
		nameFetch = memory->readVRAM(nameAddress);
		idleCounter++;
	}
}



//------------------------------------------------------------------------------------------------------------------------------------------
//Muxes
const void ppu::fourToOneMux()				//Used to refill attribute shift registers
{
	bool xBit, yBit;				//Holds the coarse X and Y bits
	bool attBit1, attBit2;				//Holds the two attrbute bits. 1 = low bit, 2 = high bit
	xBit = ppuAddress & 0x1;
	yBit = ppuAddress & 0x20;
	
	
	//Bit 0 = xBit, Bit 1 = yBit
	if(xBit == true && yBit == true)		//Choose bits 6 and 7
	{
		attBit1 = attFetch & 0x40;
		attBit2 = attFetch & 0x80;
	}
	else if(xBit)					//Choose bits 4 and 5
	{
		attBit1 = attFetch & 0x10;
		attBit2 = attFetch & 0x20;
	}
	else if(yBit)					//Choose bits 2 and 3
	{
		attBit1 = attFetch & 0x04;
		attBit2 = attFetch & 0x08;
	}
	else						//Choose bits 0 and 1
	{
		attBit1 = attFetch & 0x01;
		attBit2 = attFetch & 0x02;
	}
	
	//Sets all the bits the same
	highAttShift = attBit2 | (attBit2 << 1) | (attBit2 << 2) | (attBit2 << 3) | (attBit2 << 4)
					| (attBit2 << 5) | (attBit2 << 6) | (attBit2 << 7);
	lowAttShift = attBit1 | (attBit1 << 1) | (attBit1 << 2) | (attBit1 << 3) | (attBit1 << 4)
					| (attBit1 << 5) | (attBit1 << 6) | (attBit1 << 7);
}


const bool ppu::eightToOneMux1(unsigned short &data)		//16 bit numbers
{
	bool retval;
		
	switch(fineXScroll)
	{
		case 0:
			retval = data & 0x01;
			break;
		case 1:
			retval = data & 0x02;
			break;
		case 2:
			retval = data & 0x04;
			break;
		case 3:
			retval = data & 0x08;
			break;
		case 4:
			retval = data & 0x10;
			break;
		case 5:
			retval = data & 0x20;
			break;
		case 6:
			retval = data & 0x40;
			break;
		case 7:
			retval = data & 0x80;
			break;
	}

	return retval;
}

const bool ppu::eightToOneMux2(unsigned char &data)		//8 bit numbers
{
	bool retval;
		
	switch(fineXScroll)
	{
		case 0:
			retval = data & 0x01;
			break;
		case 1:
			retval = data & 0x02;
			break;
		case 2:
			retval = data & 0x04;
			break;
		case 3:
			retval = data & 0x08;
			break;
		case 4:
			retval = data & 0x10;
			break;
		case 5:
			retval = data & 0x20;
			break;
		case 6:
			retval = data & 0x40;
			break;
		case 7:
			retval = data & 0x80;
			break;
	}

	return retval;
}






