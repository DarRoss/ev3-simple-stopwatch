// memset, memcpy
#include <string.h>
// usleep
#include <unistd.h>
// printf
#include <stdio.h>
// fcntl
#include <fcntl.h>
// FBIOGET
#include <linux/fb.h>
// KD_GRAPHICS
#include <linux/kd.h>
// ioctl
#include <sys/ioctl.h>
// mmap
#include <sys/mman.h>
// uint
#include <stdint.h>
// timespec
#include <time.h>
// timeval
#include <sys/time.h>

#include "ev3.h"
#include "bitmaps.h"

#define BUF_SIZE 16
#define MAX_SPLITS 1
// number of nanoseconds between drawing on the screen
#define DRAW_NS 73000000

/////////////////////////////////////////////////////////////////////////
/// ENUMS
/////////////////////////////////////////////////////////////////////////

enum Button
{
	BACKSPACE 	= 14,
	ENTER 		= 28, 
	UP 			= 103,
	LEFT 		= 105,
	RIGHT 		= 106,
	DOWN 		= 108
};

enum TimerState
{
	PAUSED,
	STARTED,
	RUNNING
};

/////////////////////////////////////////////////////////////////////////
/// STRUCTS
/////////////////////////////////////////////////////////////////////////

struct FrameBufferInfo
{
	// screen dimensions
	uint32_t screenWidth;
	uint32_t screenHeight;
	// number of bytes per row
	uint32_t lineLength;
	// total bytes in the frame buffer
	uint32_t size;
	// bits per pixel
	uint32_t bitsPP;
};

/**
 * The pixel matrix is comprised of black and white mono pixel elements.
 */
struct MonoPixelElement
{
	// is a foreground pixel
	bool isFG;
	// when this is true, the pixel has been updated without having been drawn
	bool drawFlag;
};

struct InputEvent
{
	// timestamp of this event
	struct timeval time;
	// 1 means an actual event. 0 means filler that can be ignored
	uint16_t type;
	// the id of this button
	uint16_t code;
	// 1 means pressed. 0 means released
	uint32_t value;
};

/**
 * Frame buffer info and pixel matrix are frequently passed together as arguments.
 */
struct FrameBufferPixelMatrix
{
	struct FrameBufferInfo fbInfo;
	struct MonoPixelElement* pixelMatrix;
};

/**
 * Frame buffer bit information of a given pixel inside the pixel matrix.
 */
struct FramePixelBitInfo
{
	// current pixel index inside the pixel matrix
	int pixelIndex;
	// the first bit of a given pixel
	int bitStartIndex;
	// the last bit of a given pixel
	int bitEndIndex;
	// index of the byte containing the first bit of a given pixel
	int byteStartIndex;
	// index of the first pixel bit inside a byte. this is in [0, 7]
	int startIndexOffset;
	// index of the byte containing the last bit in a given pixel
	int byteEndIndex;
	// index of the last pixel bit inside a byte. this is in [0, 7]
	int endIndexOffset;
	// how many char-aligned bytes the pixel spans. 0 means it spans a single byte
	int byteRange;
	// the index of the first bit of a given row
	int rowStartBit;
};

struct TextFormat
{
	// the coordinates of the top left pixel of the top left most bit
	int posY;
	int posX;
	// scale determines bit width. ie scale of 3 means each bit is 3x3 pixels.
	int scale;
};

/**
 * Initialize values for a given TextFormat struct.
 * Param format: pointer to the struct to initialize.
 * Param x: X position.
 * Param y: Y position.
 * Param s: scale.
 */
void initTextFormat(struct TextFormat* format, int y, int x, int s)
{
	format->posY = y;
	format->posX = x;
	format->scale = s;
}

/**
 * Initialize values for a given FrameBufferPixelMatrix struct.
 * Param fbpm: pointer to the struct to initialize.
 * Param fbInfo: pointer to frame buffer info.
 * Param pixelMatrix: pointer to the pixel matrix.
 */
void initFrameBufferPixelMatrix(struct FrameBufferPixelMatrix* fbpm, 
	const struct FrameBufferInfo* fbInfo, struct MonoPixelElement* pixelMatrix)
{
	// copy information from given fbInfo struct
	fbpm->fbInfo = *fbInfo;
	fbpm->pixelMatrix = pixelMatrix;
}

/////////////////////////////////////////////////////////////////////////
/// DEBUG FUNCTIONS
/////////////////////////////////////////////////////////////////////////

/**
 * Print frame buffer info.
 * param fbInfo: Struct containing frame buffer info.
 */
void printFbInfo(const struct FrameBufferInfo* fbInfo)
{
		printf("--- Frame Buffer Info ---\n");
		printf("Screen Width: %u pixels\n", fbInfo->screenWidth);
		printf("Screen Height: %u pixels\n", fbInfo->screenHeight);
		printf("Screen Size: %u bytes\n", fbInfo->size);
		printf("Line Length: %u bytes\n", fbInfo->lineLength);
		printf("Bits per Pixel: %u bits\n", fbInfo->bitsPP);
}

/////////////////////////////////////////////////////////////////////////
/// INPUT FUNCTIONS
/////////////////////////////////////////////////////////////////////////

/**
 * Read from the input event file for an input event.
 * Param fd: input event file descriptor.
 * Return the button code of the input event. -1 if no event was found.
 */
int readInputEvent(int fd)
{
	struct InputEvent iEvent;
	int retVal = -1;
	// repeatedly read from the file until there are no more events
	// or we have found what we are looking for
	while(retVal < 0 && read(fd, &iEvent, sizeof(iEvent)) >= 0)
	{
		// event must be a button press (not a release)
		if(iEvent.type == 1 && iEvent.value == 1)
		{
			// return button code of the event
			retVal = iEvent.code;
		}
	}
	return retVal;
}

/////////////////////////////////////////////////////////////////////////
/// TIME FUNCTIONS
/////////////////////////////////////////////////////////////////////////

/**
 * Given a positive number of nanoseconds, write
 * a string with format HH:MM:SS.mmm to a given buffer.
 * param ns: the number of nanoseconds.
 * param timeStrBuf: char array buffer to be written to.
 * param bufferSize: the size of the char array buffer in bytes.
 * return true if the time was written to the string buffer.
 */
bool nsToString(int64_t ns, char* timeStrBuf, int bufferSize)
{	
	bool success;
	// buffer pointer must be non null
	// buffer size must be at least 1
	// milliseconds must be non negative
	if((success = timeStrBuf && bufferSize >= 1 && ns >= 0))
	{
		// OUTPUT_SIZE includes null terminator
		const int OUTPUT_SIZE = 13;
		if((success = bufferSize >= OUTPUT_SIZE))
		{
			int64_t ms = ns / 1000000;
			int msToPrint = ms % 1000;
			int seconds = ms / 1000;
			int secondsToPrint = seconds % 60;
			int minutes = seconds / 60;
			int minutesToPrint = minutes % 60;
			int hoursToPrint = minutes / 60;

			if(hoursToPrint > 0)
			{
				snprintf(timeStrBuf, OUTPUT_SIZE, "%d:%02d:%02d.%03d", 
					hoursToPrint, minutesToPrint, secondsToPrint, msToPrint);
			}
			else
			{
				snprintf(timeStrBuf, OUTPUT_SIZE, "%02d:%02d.%03d", 
					minutesToPrint, secondsToPrint, msToPrint);
			}
		}
		else
		{
			// buffer size insufficient
			*timeStrBuf = '\0';
		}
	}
	return success;
}

/**
 * Calculate the number of nanoseconds between two timestamps.
 * Param after: the later timestamp.
 * Param before: the earlier timestamp.
 * Return the difference between the two timestamps in nanoseconds.
 * See https://stackoverflow.com/a/64896093
 */
int64_t diffTimespecNs(const struct timespec after, const struct timespec before)
{
    return ((int64_t)after.tv_sec - (int64_t)before.tv_sec) * (int64_t)1000000000
         + ((int64_t)after.tv_nsec - (int64_t)before.tv_nsec);
}

/////////////////////////////////////////////////////////////////////////
/// DRAWING FUNCTIONS
/////////////////////////////////////////////////////////////////////////

/**
 * Write a pixel to the frame buffer given that the pixel occupies a single byte.
 * Param fbDest: the memory map location of the frame buffer.
 * Param fbpm: frame buffer info + pixel matrix.
 * Param fpbi: frame buffer bit information of a given pixel inside the pixel matrix.
 */
void writeFbSingleByte(
	char* fbDest, 
	const struct FrameBufferPixelMatrix* fbpm, 
	const struct FramePixelBitInfo* fpbi)
{
	int numShifts = 7 - fpbi->endIndexOffset;
	char mask = ((1 << fbpm->fbInfo.bitsPP) - 1) << numShifts;
	if(fbpm->pixelMatrix[fpbi->pixelIndex].isFG)
	{
		// AND mask to turn pixel black
		mask = ~mask;
		fbDest[fpbi->byteStartIndex] &= mask;
	}
	else
	{
		// OR mask to turn pixel white
		fbDest[fpbi->byteStartIndex] |= mask;
	}
}

/**
 * Write a pixel to the frame buffer given that the pixel occupies two or more bytes.
 * Param fbDest: the memory map location of the frame buffer.
 * Param fbpm: frame buffer info + pixel matrix.
 * Param fpbi: frame buffer bit information of a given pixel inside the pixel matrix.
 */
void writeFbMultiByte(
	char* fbDest, 
	const struct FrameBufferPixelMatrix* fbpm, 
	const struct FramePixelBitInfo* fpbi)
{
	// suffix byte: the byte containing the trailing bits of the pixel
	int numSuffixShifts = 7 - fpbi->endIndexOffset;
	// prefix byte: the byte containing the leading bits of the pixel
	int numPrefixShifts = 8 - fpbi->startIndexOffset;
	char prefixMask = (1 << numPrefixShifts) - 1;
	char suffixMask = ~((1 << numSuffixShifts) - 1);
	if(fbpm->pixelMatrix[fpbi->pixelIndex].isFG)
	{
		// AND mask to turn pixel black
		prefixMask = ~prefixMask;
		suffixMask = ~suffixMask;
		fbDest[fpbi->byteStartIndex] &= prefixMask;
		fbDest[fpbi->byteEndIndex] &= suffixMask;
	}
	else
	{
		// OR mask to turn pixel white
		fbDest[fpbi->byteStartIndex] |= prefixMask;
		fbDest[fpbi->byteEndIndex] |= suffixMask;
	}
	// the pixel occupies three or more bytes
	if(fpbi->byteRange >= 2)
	{
		// fill the middle byte(s)
		char color = fbpm->pixelMatrix[fpbi->pixelIndex].isFG ? 0x00 : 0xFF;
		memset(&fbDest[fpbi->byteStartIndex + 1], color, fpbi->byteRange - 1);
	}
}

/**
 * Write pixel matrix information into the memory mapped frame buffer.
 * Param fbDest: the memory map location of the frame buffer.
 * Param fbpm: frame buffer info + pixel matrix.
 */
void writeToFrameBuffer(char* fbDest, const struct FrameBufferPixelMatrix* fbpm)
{
	struct FramePixelBitInfo fpbi;

	for(unsigned row = 0; row < fbpm->fbInfo.screenHeight; ++row)
	{
		fpbi.rowStartBit = row * fbpm->fbInfo.lineLength * 8;
		for(unsigned col = 0; col < fbpm->fbInfo.screenWidth; ++col)
		{
			// determine the index of this pixel in the pixel matrix
			fpbi.pixelIndex = row * fbpm->fbInfo.screenWidth + col;
			// only proceed if the pixel needs to be drawn
			if(fbpm->pixelMatrix[fpbi.pixelIndex].drawFlag)
			{
				fpbi.bitStartIndex = fpbi.rowStartBit + col * fbpm->fbInfo.bitsPP;
				fpbi.bitEndIndex = fpbi.rowStartBit + (col + 1) * fbpm->fbInfo.bitsPP - 1;
				fpbi.byteStartIndex = fpbi.bitStartIndex / 8;
				fpbi.startIndexOffset = fpbi.bitStartIndex % 8;
				fpbi.byteEndIndex = fpbi.bitEndIndex / 8;
				fpbi.endIndexOffset = fpbi.bitEndIndex % 8;
				fpbi.byteRange = fpbi.byteEndIndex - fpbi.byteStartIndex;

				if(fpbi.byteRange == 0)
				{
					// the pixel occupies a single byte
					writeFbSingleByte(fbDest, fbpm, &fpbi);
				}
				else
				{
					// the pixel occupies two or more bytes
					// assume that the pixel does not occupy more than one row
					writeFbMultiByte(fbDest, fbpm, &fpbi);
				}
				// unset the draw flag
				fbpm->pixelMatrix[fpbi.pixelIndex].drawFlag = false;
			}
		}
	}
}

/**
 * Update a pixel in the pixel matrix.
 * Param row: row of the pixel in the matrix.
 * Param col: column of the pixel in the matrix.
 * Param isFG: if true, foreground pixel.
 * Param fbpm: frame buffer info + pixel matrix.
 */
void setPixel(
	int row, 
	int col, 
	bool isFG, 
	const struct FrameBufferPixelMatrix* fbpm)
{
	int index = row * fbpm->fbInfo.screenWidth + col;
	// check if the pixel's current color differs from its desired color
	if(fbpm->pixelMatrix[index].isFG != isFG)
	{
		fbpm->pixelMatrix[index].isFG = isFG;
		fbpm->pixelMatrix[index].drawFlag = true;
	}
}

/**
 * Draw a given bitmap somewhere on the pixel matrix.
 * Param bitmap: the bitmap to draw.
 * Param tFormat: position and scale of the bitmap.
 * Param fbpm: frame buffer info + pixel matrix.
 */
void drawBitMap(
	const char* bitmap[BITMAP_WIDTH + 1], 
	const struct TextFormat* tFormat,
	const struct FrameBufferPixelMatrix* fbpm)
{
	bool isFg;
	unsigned currY;
	unsigned currX;
	for(int bitRow = 0; bitRow < BITMAP_HEIGHT; ++bitRow)
	{
		for(int bitCol = 0; bitCol < BITMAP_WIDTH; ++bitCol)
		{
			// space char means its a background pixel
			isFg = bitmap[bitRow][bitCol] != ' ';
			// subRow: the row of pixels inside of one bitmap bit
			for(int subRow = 0; subRow < tFormat->scale; ++subRow)
			{
				currY = tFormat->posY + bitRow * tFormat->scale + subRow;
				if(currY < fbpm->fbInfo.screenHeight)
				{
					// subCol: the column of pixels inside of one bitmap bit
					for(int subCol = 0; subCol < tFormat->scale; ++subCol)
					{
						currX = tFormat->posX + bitCol * tFormat->scale + subCol;
						if(currX < fbpm->fbInfo.screenWidth)
						{
							setPixel(currY, currX, isFg, fbpm);
						}
					}
				}
			}
		}
	}
}

/**
 * Draw a string of characters on a given pixel matrix.
 * Param str: the string to draw.
 * Param tFormat: text formatting of the string.
 * Param fbpm: frame buffer info + pixel matrix.
 */
void drawString(
	char* str, 
	const struct TextFormat* tFormat, 
	const struct FrameBufferPixelMatrix* fbpm)
{
	int len = strlen(str);
	unsigned xOffset;
	const char** bitMapToDraw;
	bool inBounds = true;
	// character text formatting
	struct TextFormat charFormat;

	// loop through characters in the string
	for(int i = 0; i < len && inBounds; ++i)
	{
		xOffset = tFormat->posX + i * tFormat->scale * (BITMAP_WIDTH + BITMAP_SPACE);
		// top left corner of character bitmap must be onscreen
		if((inBounds = xOffset < fbpm->fbInfo.screenWidth))
		{
			switch(str[i])
			{
				case '0':
					bitMapToDraw = BITMAP_ZERO;
					break;
				case '1':
					bitMapToDraw = BITMAP_ONE;
					break;
				case '2':
					bitMapToDraw = BITMAP_TWO;
					break;
				case '3':
					bitMapToDraw = BITMAP_THREE;
					break;
				case '4':
					bitMapToDraw = BITMAP_FOUR;
					break;
				case '5':
					bitMapToDraw = BITMAP_FIVE;
					break;
				case '6':
					bitMapToDraw = BITMAP_SIX;
					break;
				case '7':
					bitMapToDraw = BITMAP_SEVEN;
					break;
				case '8':
					bitMapToDraw = BITMAP_EIGHT;
					break;
				case '9':
					bitMapToDraw = BITMAP_NINE;
					break;
				case ':':
					bitMapToDraw = BITMAP_COLON;
					break;
				case '.':
					bitMapToDraw = BITMAP_PERIOD;
					break;
				default:
					bitMapToDraw = BITMAP_X;
					break;
			}
			initTextFormat(&charFormat, tFormat->posY, xOffset, tFormat->scale);
			drawBitMap(bitMapToDraw, &charFormat, fbpm);
		}
	}
}

/////////////////////////////////////////////////////////////////////////
/// INIT FUNCTIONS
/////////////////////////////////////////////////////////////////////////

/**
 * Prevent terminal text from appearing onscreen.
 * return true if graphics mode was successfully enabled.
 * See https://github.com/ev3dev/ev3dev/issues/1643#issuecomment-2237762929
 */
bool enableGraphicsMode()
{
	bool success;
	// Get the file descriptor for the virtual terminal 
	// (started when using 'brickrun' from SSH, or when running program from the brick)
    int fd = open("/dev/tty", O_RDWR);
    if ((success = fd >= 0))
	{
    	// Set the virtual terminal to "graphics mode", 
		// to prevent terminal text from showing up on the screen
    	success = !ioctl(fd, KDSETMODE, KD_GRAPHICS);
		close(fd);
	}
	else
	{
        printf("Error opening tty descriptor\n");
	}
	return success;
}

/**
 * Memory map the frame buffer file.
 * Param fbInfo: pointer to frame buffer info.
 * Param fbDest: pointer to pointer of memory map location.
 * return true if memory mapping was successful.
 */
bool setupMmap(const struct FrameBufferInfo* fbInfo, char** fbDest)
{
	bool success;
	int fd = open("/dev/fb0", O_RDWR | O_CLOEXEC);
	if((success = fd >= 0))
	{
		// memory map the frame buffer
		*fbDest = mmap(0, fbInfo->size, PROT_WRITE, MAP_SHARED, fd, 0);
		if((success = *fbDest != MAP_FAILED))
		{
			// clear frame buffer before use
			memset(*fbDest, 0xFF, fbInfo->size);
		}
		else
		{
		    printf("Error creating memory map\n");
		}
		close(fd);
	}
	else
	{
        printf("Error opening framebuffer device\n");
	}
	return success;
}

/**
 * Read frame buffer info using ioctl and write into a given struct.
 * param fbInfo: Frame buffer info is written into this struct.
 * return: True if all info was successfully written into struct.
 * See https://stackoverflow.com/q/75412675
 */
bool loadFrameValues(struct FrameBufferInfo* fbInfo)
{
	bool success = true;
	struct fb_fix_screeninfo fb_fix;
	struct fb_var_screeninfo fb_var;
	int fd = open("/dev/fb0", O_RDWR | O_CLOEXEC);
    if ((success = fd >= 0))
	{
	    // Get fixed info about fb
	    if ((success = !ioctl(fd,FBIOGET_FSCREENINFO,&fb_fix)))
		{
	    	// Get variable info about fb
	    	if ((success = !ioctl(fd,FBIOGET_VSCREENINFO,&fb_var)))
			{
				fbInfo->size = fb_fix.smem_len;
				fbInfo->lineLength = fb_fix.line_length;
				fbInfo->screenWidth = fb_var.xres;
				fbInfo->screenHeight = fb_var.yres;
				fbInfo->bitsPP = fb_var.bits_per_pixel;
	    	}
			else
			{
	    	    printf("Error reading variable FB information\n");
			}
	    }
		else
		{
	        printf("Error reading fixed FB information\n");
		}
		close(fd);
    }
	else
	{
        printf("Error opening framebuffer descriptor\n");
	}
	return success;
}

/////////////////////////////////////////////////////////////////////////
/// PROCESS FUNCTIONS
/////////////////////////////////////////////////////////////////////////

/**
 * Perform calculations based on timer state.
 * Param state: current state of the timer.
 * Param elapsedNs: current time value of the timer.
 * Param titleFormat: text formatting for timer string.
 * Param fbDest: frame buffer memory map location. 
 * Param fbpm: frame buffer info + pixel matrix.
 */
void processTimer(
	enum TimerState* state, 
	int64_t* elapsedNs, 
	const struct TextFormat* titleFormat, 
	char* fbDest, 
	const struct FrameBufferPixelMatrix* fbpm)
{
	// timestamps that persist between function calls
	static struct timespec prevTs, currTs, drawTs;
	static char strBuf[BUF_SIZE];

	if(*state != PAUSED)
	{
		if(*state == STARTED)
		{
			*state = RUNNING;
			// bring previous timestamp up to date
			clock_gettime(CLOCK_MONOTONIC, &prevTs);
			drawTs = prevTs;
		}
		else
		{
			clock_gettime(CLOCK_MONOTONIC, &currTs);
			*elapsedNs += diffTimespecNs(currTs, prevTs);
			// check if its time to draw
			if(diffTimespecNs(currTs, drawTs) >= DRAW_NS)
			{
				nsToString(*elapsedNs, strBuf, BUF_SIZE);
				drawString(strBuf, titleFormat, fbpm);
				writeToFrameBuffer(fbDest, fbpm);
				drawTs = currTs;
			}
			prevTs = currTs;
		}
	}
}

/**
 * Read and process input events.
 * Param state: current state of the timer.
 * Param elapsedNs: current timer value.
 * Param titleFormat: text formatting for timer title.
 * Param splitFormat: text formatting for split timestamp.
 * Param inputFd: file descriptor for input event file.
 * Param fbDest: frame buffer memory map location.
 * Param fbpm: frame buffer info + pixel matrix.
 * Return true if the user wants to quit.
 */
bool pollInput(
	enum TimerState* state, 
	int64_t* elapsedNs, 
	const struct TextFormat* titleFormat,
	const struct TextFormat* splitFormat, 
	int inputFd, 
	char* fbDest, 
	const struct FrameBufferPixelMatrix* fbpm)
{
	static char strBuf[BUF_SIZE];
	static int64_t splits[MAX_SPLITS];
	static int nextSplitIndex = 0;
	enum Button btnCode = readInputEvent(inputFd);
	bool isExit = false;

	switch(btnCode)
	{
		case BACKSPACE:
			isExit = true;
			break;
		case ENTER:
			if(*state == PAUSED)
			{
				*state = STARTED;
			}
			else
			{
				nsToString(*elapsedNs, strBuf, BUF_SIZE);
				drawString(strBuf, titleFormat, fbpm);
				writeToFrameBuffer(fbDest, fbpm);
				*state = PAUSED;
			}
			break;
		case UP:
			// TODO view previous split
			break;
		case DOWN:
			// TODO view next split
			break;
		case LEFT:
			if(*state == PAUSED)
			{
				*elapsedNs = 0;
				nextSplitIndex = 0;
				nsToString(0, strBuf, BUF_SIZE);
				drawString(strBuf, titleFormat, fbpm);
				drawString(strBuf, splitFormat, fbpm);
				writeToFrameBuffer(fbDest, fbpm);
			}
			break;
		case RIGHT:
			if(*state == RUNNING)
			{
				splits[nextSplitIndex] = *elapsedNs;
				nsToString(splits[nextSplitIndex], strBuf, BUF_SIZE);
				drawString(strBuf, splitFormat, fbpm);
				writeToFrameBuffer(fbDest, fbpm);
				nextSplitIndex = (nextSplitIndex + 1) % MAX_SPLITS;
			}
			break;
		default:
			// do nothing
			break;
	}
	return isExit;
}

/////////////////////////////////////////////////////////////////////////
/// MAIN FUNCTIONS
/////////////////////////////////////////////////////////////////////////

/**
 * Main processing loop.
 * Param fbInfo: pointer to frame buffer info.
 * Param fbDest: pointer to memory mapped frame buffer.
 * Param inputFd: file descriptor for input events file.
 */
void performMainLoop(struct FrameBufferInfo* fbInfo, char* fbDest, int inputFd)
{
	static char strBuf[BUF_SIZE];
	enum TimerState state = PAUSED;
	struct MonoPixelElement pixelMatrix[fbInfo->screenHeight * fbInfo->screenWidth];
	struct TextFormat titleFormat, splitFormat;
	struct FrameBufferPixelMatrix fbpm;
	int64_t elapsedNs = 0;
	bool isExit = false;

	// pre-loop inits
	memset(pixelMatrix, 0, sizeof(pixelMatrix));
	nsToString(0, strBuf, BUF_SIZE);
	// frame buffer info is copied into fbpm struct
	initFrameBufferPixelMatrix(&fbpm, fbInfo, pixelMatrix);
	initTextFormat(&titleFormat, 16, 16, 3);
	initTextFormat(&splitFormat, 48, 16, 2);
	drawString(strBuf, &titleFormat, &fbpm);
	drawString(strBuf, &splitFormat, &fbpm);
	writeToFrameBuffer(fbDest, &fbpm);

	do
	{
		processTimer(&state, &elapsedNs, &titleFormat, fbDest, &fbpm);
		// one tick per millisecond
		usleep(1000);
		isExit = pollInput(&state, &elapsedNs, &titleFormat, &splitFormat, 
			inputFd, fbDest, &fbpm);
	} while(!isExit);
}

/**
 * Perform initializations on the given pointers.
 * Param fbInfo: pointer to frame buffer info struct.
 * Param fbDest: pointer to a pointer to the memory mapped frame buffer destination.
 * Param inputFd: pointer to the input event file descriptor.
 * Return true if initialization was a success.
 */
bool initMain(struct FrameBufferInfo* fbInfo, char** fbDest, int* inputFd)
{
	bool success;
	// enable graphics mode
	if((success = enableGraphicsMode()))
	{
		// obtain the input event file descriptor
    	*inputFd = open("/dev/input/by-path/platform-gpio_keys-event", O_RDONLY | O_NONBLOCK);
		if((success = *inputFd >= 0))
		{
			// load frame buffer values into struct
			if((success = loadFrameValues(fbInfo)))
			{
				// setup memory map
				success = setupMmap(fbInfo, fbDest);
			}
		}
	}
	return success;
}

int main(int argc, char** argv)
{
	(void)argc;
	(void)argv;

	bool success;
	struct FrameBufferInfo fbInfo;
	int inputFd;
	char* fbDest;

	if((success = ev3_init() >= 1))
	{
		if((success = initMain(&fbInfo, &fbDest, &inputFd)))
		{
			performMainLoop(&fbInfo, fbDest, inputFd);
		}
		else
		{
			printf("Main init failed\n");
		}
		ev3_uninit();
	}
	else
	{
		printf("EV3 failed to init\n");
	}
	return success ? 0 : 1;
}
