///////////////////////////////////////////////////////////////
//
//  
// 
///////////////////////////////////////////////////////////////

/*--------------------------------------------------------------------------
 INCLUDE FILES
 --------------------------------------------------------------------------*/
#include <stdio.h>
#include "MyTypes.h"

/*--------------------------------------------------------------------------
 MODULE CONSTANTS
 --------------------------------------------------------------------------*/
// Pin 13 has an LED connected on most Arduino boards.
// give it a name:
#define PIN_STATUS_LED				(13)

#define TIMER_TICK					(1)

// Delimiters  for start and end of message
#define START_DELIMITER				'<'
#define END_DELIMITER				'>'

const uint16_t RD_BUFFER_SIZE		= 200;
const uint16_t WR_BUFFER_SIZE		= 200;

/*--------------------------------------------------------------------------
 MODULE MACROS
 --------------------------------------------------------------------------*/

/*--------------------------------------------------------------------------
 MODULE DATA TYPES
 --------------------------------------------------------------------------*/

/*--------------------------------------------------------------------------
 MODULE VARIABLES
 --------------------------------------------------------------------------*/

static Timer_t m_TimerISRCnt;			// Timer tick count
static volatile uint32_t m_TimerCounter;			// Reported back to UI;

static bool m_Sample;						// when true and m_cycling true, ADC sampling occurss
static bool m_Blink;						// when true blinking occurss
static uint16_t m_SampleTimeMsecs = 50;		// default sample time; can be changed by Windows app


/*--------------------------------------------------------------------------
 MODULE PROTOTYPES
 --------------------------------------------------------------------------*/

// the setup routine runs once when power is applied or reset is  pressed
void setup() 
{
	// initialize the digital pin as an output.
	pinMode(PIN_STATUS_LED, OUTPUT);
	// setup serial port to intercept PC commands
	Serial.begin(115200);

	while (!Serial) {}

	// Initialize timer 2 to fire every millisecond; cant use Timer 1 because it conflicts with the software
	// serial library that is used to communicate with the display
	SetupTimer2();

	Serial.println("hello world");

}

// the loop routine runs over and over again forever:
void loop() 
{
	TimeStamp();
	ProcessSerialInput();
}


////////////////////////////////////////////////////////////////////////
//
// Set up Arduino timer2 to fire every 1 msec (downloaded from Web)
// http://arduinomega.blogspot.com/2011/05/timer2-and-overflow-interrupt-lets-get.html
//
////////////////////////////////////////////////////////////////////////
ISR(TIMER2_OVF_vect) {
	m_TimerISRCnt++;
	m_TimerCounter++;
	TCNT2 = 130;           //Reset Timer to 130 out of 255
	TIFR2 = 0x00;          //Timer2 INT Flag Reg: Clear Timer Overflow Flag
}

////////////////////////////////////////////////////////////////////////
//
// Set up Arduino timer2 to fire every 1 msec (downloaded from Web)
// http://arduinomega.blogspot.com/2011/05/timer2-and-overflow-interrupt-lets-get.html
//
////////////////////////////////////////////////////////////////////////
void SetupTimer2() 
{
	//Setup Timer2 to fire every 1ms
	TCCR2B = 0x00;        //Disable Timer2 while we set it up
	TCNT2  = 130;         //Reset Timer Count to 130 out of 255
	TIFR2  = 0x00;        //Timer2 INT Flag Reg: Clear Timer Overflow Flag
	TIMSK2 = 0x01;        //Timer2 INT Reg: Timer2 Overflow Interrupt Enable
	TCCR2A = 0x00;        //Timer2 Control Reg A: Wave Gen Mode normal
	TCCR2B = 0x05;        //Timer2 Control Reg B: Timer Prescaler set to 128
}

////////////////////////////////////////////////////////////////////////
//
// Used for timing (no need to call "delay" to pause all processing)
//
////////////////////////////////////////////////////////////////////////
Timer_t ElapsedTime(Timer_t aOldTime)
{
	Timer_t calcTime, currentTicks;

	currentTicks = m_TimerISRCnt;

	if (aOldTime > currentTicks) /* clock rolled over  */
	{
		/*------------------------------------------------------------------
		 ; If the clock has rolled over, the time elapsed is calculated by the
		 ; following formula.  The 1 extra addition is for the tick when the
		 ; clock rolls from maximum to 0.  If the +1 was not added, the expired
		 ; time would be off by 1 tick at rollovers.
		 ------------------------------------------------------------------*/
		calcTime = (MAX_TIMER_VALUE - aOldTime) + currentTicks + 1;
	}
	else /* clock has not rolled over       */
	{
		calcTime = currentTicks - aOldTime;
	}
	return (calcTime);
}


////////////////////////////////////////////////////////////////////////
//
// Used to create a timestamp for all collected data and perform timing
// operations
//
////////////////////////////////////////////////////////////////////////
void TimeStamp(void)
{
	static Timer_t ledTimer = 0;
	static Timer_t sampleTimer = 0;

	if (ElapsedTime(ledTimer) >= 500)
	{
		m_Blink = true;
		BlinkLED();
		ledTimer = ElapsedTime(0);
	}
	
	if (ElapsedTime(sampleTimer) >= m_SampleTimeMsecs)
	{
		m_Sample = true;
		sampleTimer = ElapsedTime(0);
	}

}

////////////////////////////////////////////////////////////////////////
//
// Blinks the LEDs 
//
////////////////////////////////////////////////////////////////////////
void BlinkLED(void)
{
	static boolean ledState;

	if (ledState) // Blink on 
	{
		digitalWrite(PIN_STATUS_LED, HIGH);   // turn the LED on (HIGH is the voltage level)
		ledState = false;
	}
	else
	{
		digitalWrite(PIN_STATUS_LED, LOW);   // turn the LED off (LOW is the voltage level)
		ledState = true;
	}      

}


////////////////////////////////////////////////////////////////////////
//
// Reads serial data from UI until an end delimiter is encountered
//
////////////////////////////////////////////////////////////////////////
void ProcessSerialInput(void)
{
	static char serialBuffer[RD_BUFFER_SIZE];
	static uint16_t bufIndex = 0;

	// Read available characters in the serial port
	while (Serial.available() > 0)
	{
		serialBuffer[bufIndex] = Serial.read();  
		if (serialBuffer[bufIndex] == END_DELIMITER)
		{
			serialBuffer[bufIndex + 1] = '\0';       // Terminate String if end delimiter found 
			ProcessLabviewInput(serialBuffer);          // Take action based on command sent from UI
			bufIndex = 0;
		}
		else
		{
			bufIndex++;
			if (bufIndex >= RD_BUFFER_SIZE)
			{
				bufIndex = 0;  
			}
		}
	}
}

const uint8_t MAX_INS_FROM_LABVIEW = 8;

static int32_t m_Id[MAX_INS_FROM_LABVIEW];
static int32_t m_Value[MAX_INS_FROM_LABVIEW];
////////////////////////////////////////////////////////////////////////
//
// Processes String commands from the UI
//
////////////////////////////////////////////////////////////////////////
boolean ProcessLabviewInput(char *aBuffer)
{
	
	boolean startDelimiterFound = false;
	uint16_t idCount;
	char numericString[RD_BUFFER_SIZE];

	if(*aBuffer != START_DELIMITER)
	{
		return false;    // Don't do anything if start delimiter isn't the first character
	}

	char labviewCmd = aBuffer[1];

	// Remove the first 2 chars char
	strcpy(numericString, &aBuffer[2]);

	// Remove the last char
	numericString[strlen(numericString) - 1] = 0;

	switch (labviewCmd)
	{
		case 's':
		case 'S':
			idCount = PopulateIds(numericString);
			// Populate m_Id[] so all channels are sent
			if (idCount == 0)
			{
				m_Id[0] = 0; m_Id[1] = 1; m_Id[2] = 2; m_Id[3] = 3;
			}
			SendChannels(); 
			break;

		case 'w':
		case 'W':
			PopulateIdsAndValues(numericString);
			// WriteParameters()
			break;

		case 'r':
		case 'R':
			// PopulateIds();
			// ReadParameters()
			break;

	}


	return true;

}



uint16_t PopulateIds (char *labviewString)
{

	// Clear array info
	for (uint8_t i = 0; i < MAX_INS_FROM_LABVIEW; i++)
	{
		m_Id[i] = -1;
		m_Value[i] = -1;
	}

	if (strlen(labviewString) == 0)
	{
		return 0;
	}

	int16_t strLength = strlen(labviewString);
	// Add a comma for consistent parsing
	labviewString[strLength] = ',';
	labviewString[strLength + 1] = 0;

	strLength = strlen(labviewString);

	// Count the number of commas, the number of commas + 1 is the amount of ids and/or values sent from labview
	uint16_t index = 0; 
	uint16_t commaCount = 0;

	while (index < strLength)
	{
		if (labviewString[index] == ',')
		{
			commaCount++;	
		}
		index++;
	}

    uint16_t count = 0;
	char *ptr = labviewString;
	char val[20];

	index = 0;
	while (count < commaCount)
	{
	    while (ptr[index] != ',')
		{
			index++;
		}

		strncpy(val, ptr, index);

		m_Id[count] = strtol(val, NULL, 10);

		ptr = &ptr[index + 1];
		index = 0;

		count++;
	}

	return commaCount;

}

uint16_t PopulateIdsAndValues (char *labviewString)
{
	// Clear array info
	for (uint8_t i = 0; i < MAX_INS_FROM_LABVIEW; i++)
	{
		m_Id[i] = -1;
		m_Value[i] = -1;
	}

	if (strlen(labviewString) == 0)
	{
		return 0;
	}

	int16_t strLength = strlen(labviewString);
	// Add a comma for consistent parsing
	labviewString[strLength] = ',';
	labviewString[strLength + 1] = 0;

	strLength = strlen(labviewString);

	// Count the number of commas, the number of commas + 1 is the amount of ids and/or values sent from labview
	uint16_t index = 0; 
	uint16_t commaCount = 0;

	while (index < strLength)
	{
		if (labviewString[index] == ',')
		{
			commaCount++;	
		}
		index++;
	}

    uint16_t count = 0;
	char *ptr = labviewString;
	char val[20];

	index = 0;
	boolean updateId = true;
	while (count < (commaCount / 2))
	{
	    while (ptr[index] != ',')
		{
			index++;
		}

		strncpy(val, ptr, index);

		if (updateId == true)
		{
			m_Id[count] = strtol(val, NULL, 10);
			updateId = false;
		}
		else
		{
			m_Value[count] = strtol(val, NULL, 10);
			updateId = true;
			count++;
		}

		ptr = &ptr[index + 1];
		index = 0;
	}

	return commaCount / 2;

}


uint16_t tempData[4] = {0 , 12, 345, 6789}; 
void SendChannels()
{
	char outStr[WR_BUFFER_SIZE] = {NULL};
	strcat (outStr, "<s");
	uint16_t index = 0;
	while (m_Id[index] != -1)
	{
		char intStr[10];
		sprintf(intStr,"%d,",tempData[m_Id[index]]);
		strcat (outStr, intStr);
		index++;
	}
	// strip the final ","
	outStr[strlen(outStr) - 1] = NULL; 

	strcat (outStr, ">");
	Serial.print(outStr);	

}



