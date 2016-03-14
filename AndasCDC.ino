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

/*--------------------------------------------------------------------------
 MODULE MACROS
 --------------------------------------------------------------------------*/

/*--------------------------------------------------------------------------
 MODULE DATA TYPES
 --------------------------------------------------------------------------*/

/*--------------------------------------------------------------------------
 MODULE VARIABLES
 --------------------------------------------------------------------------*/

static bool m_Cycling;					// when true and m_Sample true, ADC sampling occurs
static Timer_t m_TimerISRCnt;			// Timer tick count
static volatile uint32_t m_TimerCounter;			// Reported back to UI;

static bool m_Sample;						// when true and m_cycling true, ADC sampling occurss
static bool m_Blink;						// when true blinking occurss
static uint16_t m_SampleTimeMsecs = 50;		// default sample time; can be changed by Windows app

// This object is returned from function CreateResponse. It can't be 
// an object placed on the stack (local variable) for that function; 
// otherwise software becomes unstable or locks up
static String m_Response = String();

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

	// Initialize timer 2 to fire every millisecond; cant use Timer 1 because it conflicts with the software
	// serial library that is used to communicate with the display
	SetupTimer2();

	Serial.println("hello world");

}

// the loop routine runs over and over again forever:
void loop() 
{

	TimeStamp();
	if (m_Sample)			// becomes true when sample time expired
	{
		m_Sample = false;
		if (m_Cycling)		// controlled by Windows app
		{
			SendResponse();	// only send response when Windows App connected
		}
	}
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
// Used to service any objects that require blinking
//
////////////////////////////////////////////////////////////////////////
void ServiceBlinkingObjects(void)
{
	static bool blinkSync = false;	// toggles every call when m_Blink == true

	if (m_Blink == true)			// becomes true when blink cycle time expired
	{
		m_Blink = false;			// reset the flag
		blinkSync = !blinkSync;		// Toggle the state
	}
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
	const uint16_t RD_BUFFER_SIZE = 200;

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

	if(*aBuffer != START_DELIMITER)
	{
		return false;    // Don't do anything if start delimiter isn't the first character
	}


	String labviewString = String(aBuffer);
	
	// verify string starts with "<" 
	if (!labviewString.startsWith("<"))
	{
		return false;
	}

	labviewString.replace("<","");
	labviewString.replace(">","");

	Serial.println(labviewString);


	char labviewCmd = labviewString[0];

	Serial.print("labviewCmd = ");
	Serial.println(labviewCmd);

	// Strip off the Labview command
	labviewString.remove(0,1);

	Serial.println("labviewString = " + labviewString);

	switch (labviewCmd)
	{
		case 's':
		case 'S':
			idCount = PopulateIds(labviewString);
			// Populate m_Id[] so all channels are sent
			if (idCount == 0)
			{
				Serial.println("idcount = 0");
				m_Id[0] = 0; m_Id[1] = 1; m_Id[2] = 2; m_Id[3] = 3;
			}
			SendChannels(); 
			break;

		case 'w':
		case 'W':
			PopulateIdsAndValues(labviewString);
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



uint16_t PopulateIds (String labviewString)
{

	// Clear array info
	for (uint8_t i = 0; i < MAX_INS_FROM_LABVIEW; i++)
	{
		m_Id[i] = -1;
		m_Value[i] = -1;
	}

	Serial.println("PopulateIds " + labviewString); 

	if (labviewString == "")
	{
		Serial.println("No Ids");
		return 0;
	}

	// Add a comma for consistent parsing
	labviewString.concat(",");
	Serial.println("labviewString.concat(,) = " + labviewString);

	// Count the number of commas, the number of commas + 1 is the amount of ids and/or values sent from labview
	uint16_t index = 0; 
	uint16_t strLength = labviewString.length();
	uint16_t commaCount = 0;
	while (index < strLength)
	{
		if (labviewString[index] == ',')
		{
			commaCount++;	
		}
		index++;
	}

	uint16_t endCommaIndex = 0;
    uint16_t count = 0;
	while (count < commaCount)
	{
		endCommaIndex = labviewString.indexOf(",");
		// Convert all stringData to index and place in m_Id array
		String val = labviewString.substring(0, endCommaIndex);
		Serial.println("val = " + val);

		m_Id[count] = val.toInt();

		labviewString.remove(0, endCommaIndex + 1);
		Serial.println(m_Id[count]);
		count++;
	}


	return commaCount;

}

uint16_t PopulateIdsAndValues (String labviewString)
{

	// Clear array info
	for (uint8_t i = 0; i < MAX_INS_FROM_LABVIEW; i++)
	{
		m_Id[i] = -1;
		m_Value[i] = -1;
	}

	Serial.println("PopulateIdsAndValues " + labviewString); 

	// Add a comma for consistent parsing
	labviewString.concat(",");
	Serial.println("labviewString.concat(,) = " + labviewString);

	// Count the number of commas, the number of commas + 1 is the amount of ids and/or values sent from labview
	uint16_t index = 0; 
	uint16_t strLength = labviewString.length();
	uint16_t commaCount = 0;
	while (index < strLength)
	{
		if (labviewString[index] == ',')
		{
			commaCount++;	
		}
		index++;
	}

	uint16_t endCommaIndex = 0;
    uint16_t count = 0;
	boolean updateId = true;
	while (count < (commaCount / 2))
	{
		endCommaIndex = labviewString.indexOf(",");
		// Convert all stringData to index and place in m_Id array
		String val = labviewString.substring(0, endCommaIndex);
		Serial.println("val = " + val);

		if (updateId == true)
		{
			m_Id[count] = val.toInt();
			updateId = false;
		}
		else
		{
			m_Value[count] = val.toInt();
			updateId = true;
			count++;
		}

		labviewString.remove(0, endCommaIndex + 1);
	}

	return commaCount / 2;

}


uint16_t tempData[4] = {0 , 12, 345, 6789}; 
void SendChannels()
{
	Serial.println("SendChannels()");
	String outStr = "<s";
	uint16_t index = 0;
	while (m_Id[index] != -1)
	{
		String intStr = String(tempData[m_Id[index]]);
		outStr.concat(intStr);
		outStr.concat(",");
		index++;
	}
	// strip the final ","
	outStr[outStr.length() - 1] = NULL; 

	outStr.concat(">");
	Serial.print(outStr);	

}

////////////////////////////////////////////////////////////////////////
//
//  Compiles and transmits all sampled data back to the UI
//
////////////////////////////////////////////////////////////////////////
void SendResponse()
{
	String response = String();
	// append sensor values 
	response += CreateResponse(0);
	response += CreateResponse(1);
	Serial.print(response);             // send data to UI 
}


////////////////////////////////////////////////////////////////////////
//
//  Creates a response in the UI expected format with ID followed
//  by sampled ADC value followed by timestamp
//
////////////////////////////////////////////////////////////////////////
String CreateResponse(uint16_t aId)
{
	uint16_t id = aId + 1;			// needed here to avoid any potential 
									// operator overload issue with the "+"
									// See Arduino String class issues

	m_Response = "";

	//send sensorValue back to UI
	m_Response += START_DELIMITER;    // Start delimeter
	m_Response += 'S';                // Indicate sensor data
	m_Response += id;				  // add 1 to the ID 
	m_Response += ":";                 // isolate data from command
	m_Response += END_DELIMITER;      // end delimiter

	return m_Response;
}

////////////////////////////////////////////////////////////////////////
//
//  Converts a 3 digit decimal ASCII string from ASCII to a value
//
////////////////////////////////////////////////////////////////////////
uint16_t Convert3DigitASCIIToValue(char *aBuffer)
{
	return ((aBuffer[0] - '0') * 100) + ((aBuffer[1] - '0') * 10) + (aBuffer[2] - '0');
}


