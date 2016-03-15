/*--------------------------------------------------------------------------
 INCLUDE FILES
 --------------------------------------------------------------------------*/
#include <stdio.h>
#include "MyTypes.h"
#include <Wire.h>

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

#define I2C_PCA9543A_ADDR           0x70  // (0xE0 >> 1)
#define CDC_1						0x01
#define CDC_2						0x02
#define CDC_BOTH					0x03

#define I2C_AD7746_ADDR				0x48  // (0x90 >> 1)
#define REGISTER_STATUS				0x00
#define REGISTER_CAP_DATA			0x01
#define REGISTER_VT_DATA			0x04
#define REGISTER_CAP_SETUP			0x07
#define REGISTER_VT_SETUP			0x08
#define REGISTER_EXC_SETUP			0x09
#define REGISTER_CONFIGURATION		0x0A
#define REGISTER_CAP_DAC_A			0x0B
#define REGISTER_CAP_DAC_B			0x0B
#define REGISTER_CAP_OFFSET			0x0D
#define REGISTER_CAP_GAIN			0x0F
#define REGISTER_VOLTAGE_GAIN		0x11

#define RESET_ADDRESS				0xBF

#define VALUE_UPPER_BOUND			16000000L
#define VALUE_LOWER_BOUND			0xFL
#define MAX_OUT_OF_RANGE_COUNT		3
#define CALIBRATION_INCREASE		1

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

static bool m_Sample;						// when true and m_cycling true, ADC sampling occurs
static bool m_Blink;						// when true blinking occurs
static uint16_t m_SampleTimeMsecs = 50;		// default sample time; can be changed by Windows app

byte calibration;
byte outOfRangeCount = 0;
unsigned long offset = 0;
/*--------------------------------------------------------------------------
 MODULE PROTOTYPES
 --------------------------------------------------------------------------*/

// the setup routine runs once when power is applied or reset is  pressed
void setup()
{
    // initialize the digital pin as an output.
    pinMode (PIN_STATUS_LED, OUTPUT);
    // setup serial port to intercept PC commands
    Serial.begin (115200);

    while (!Serial) {}

    Serial.println ("hello world");


    // Initialize timer 2 to fire every millisecond; cant use Timer 1 because it conflicts with the software
    // serial library that is used to communicate with the display
    SetupTimer2();

    Wire.begin();

#if 0
    // Set Switch to allow the I2C bus to go to both CDC's
    Wire.beginTransmission (I2C_PCA9543A_ADDR);	// start i2c cycle
    Wire.write (CDC_BOTH);						// allow writes to go to both CDCs
    Wire.endTransmission();						// ends i2c cycle
#endif

    Wire.beginTransmission (I2C_AD7746_ADDR);	// start i2c cycle
    Wire.write (RESET_ADDRESS);					// reset the CDCs
    Wire.endTransmission();						// ends i2c cycle

    // wait for reboot
    delay (1);

    //writeRegister (REGISTER_EXC_SETUP, _BV (5) | _BV (1) | _BV (0)); // EXC source B
    //writeRegister (REGISTER_CAP_SETUP, _BV (7)); // cap setup reg - cap enabled

    Serial.println ("Getting offset");
    offset = ((unsigned long)readInteger (REGISTER_CAP_OFFSET)) << 8;
    Serial.print ("Factory offset: ");
    Serial.println (offset);

    // set configuration to calib. mode, slow sample
    //writeRegister (0x0A, _BV (7) | _BV (6) | _BV (5) | _BV (4) | _BV (3) | _BV (2) | _BV (0));

    // wait for calibration
    delay (10);

#if 0
    displayStatus();

    Serial.print ("Calibrated offset: ");
    offset = ((unsigned long)readInteger (REGISTER_CAP_OFFSET)) << 8;
    Serial.println (offset);


    writeRegister (REGISTER_CAP_SETUP, _BV (7)); // cap setup reg - cap enabled
    writeRegister (REGISTER_EXC_SETUP, _BV (3)); // EXC source A
    writeRegister (REGISTER_CONFIGURATION, _BV (7) | _BV (6) | _BV (5) | _BV (4) | _BV (3) | _BV (0)); // continuous mode

    displayStatus();
    calibrate();

    Serial.println ("done");
#endif
}

// the loop routine runs over and over again forever:
void loop()
{
    TimeStamp();
    ProcessSerialInput();

#if 0
  long value = readValue();
  Serial.print(offset);
  Serial.print("/");
  Serial.print((int)calibration);
  Serial.print("/");
  Serial.println(value);
  if ((value<VALUE_LOWER_BOUND) or (value>VALUE_UPPER_BOUND)) {
    outOfRangeCount++;
  }
  if (outOfRangeCount>MAX_OUT_OF_RANGE_COUNT) {
    if (value < VALUE_LOWER_BOUND) {
      calibrate(-CALIBRATION_INCREASE);
    } 
    else {
      calibrate(CALIBRATION_INCREASE);
    }
    outOfRangeCount=0;
  }

  delay(500);
#endif
}


////////////////////////////////////////////////////////////////////////
//
// Set up Arduino timer2 to fire every 1 msec (downloaded from Web)
// http://arduinomega.blogspot.com/2011/05/timer2-and-overflow-interrupt-lets-get.html
//
////////////////////////////////////////////////////////////////////////
ISR (TIMER2_OVF_vect)
{
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
Timer_t ElapsedTime (Timer_t aOldTime)
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
void TimeStamp (void)
{
    static Timer_t ledTimer = 0;
    static Timer_t sampleTimer = 0;

    if (ElapsedTime (ledTimer) >= 500)
    {
        m_Blink = true;
        BlinkLED();
        ledTimer = ElapsedTime (0);
    }

    if (ElapsedTime (sampleTimer) >= m_SampleTimeMsecs)
    {
        m_Sample = true;
        sampleTimer = ElapsedTime (0);
    }

}

////////////////////////////////////////////////////////////////////////
//
// Blinks the LEDs
//
////////////////////////////////////////////////////////////////////////
void BlinkLED (void)
{
    static boolean ledState;

    if (ledState) // Blink on
    {
        digitalWrite (PIN_STATUS_LED, HIGH);  // turn the LED on (HIGH is the voltage level)
        ledState = false;
    }
    else
    {
        digitalWrite (PIN_STATUS_LED, LOW);  // turn the LED off (LOW is the voltage level)
        ledState = true;
    }

}


////////////////////////////////////////////////////////////////////////
//
// Reads serial data from UI until an end delimiter is encountered
//
////////////////////////////////////////////////////////////////////////
void ProcessSerialInput (void)
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
            ProcessLabviewInput (serialBuffer);         // Take action based on command sent from UI
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
boolean ProcessLabviewInput (char *aBuffer)
{

    boolean startDelimiterFound = false;
    uint16_t idCount;
    char numericString[RD_BUFFER_SIZE];

    if (*aBuffer != START_DELIMITER)
    {
        return false;    // Don't do anything if start delimiter isn't the first character
    }

    // Save the labview command "<x" where x is the command
    char labviewCmd = aBuffer[1];

    // Remove the first 2 chars from the labview command and copy the remainder of the string
    strcpy (numericString, &aBuffer[2]);

    // Remove the last char ">"
    numericString[strlen (numericString) - 1] = NULL;

    switch (labviewCmd)
    {
        case 's':
        case 'S':
            idCount = PopulateIds (numericString);
            // idCount will be 0 if "<s>" is sent from labview so
            // Populate m_Id[] so all 4 channels are sent back to labview
            if (idCount == 0)
            {
                m_Id[0] = 0;
                m_Id[1] = 1;
                m_Id[2] = 2;
                m_Id[3] = 3;
            }
            SendChannels();
            break;

        case 'w':
        case 'W':
            PopulateIdsAndValues (numericString);
            WriteParameters();
            break;

        case 'r':
        case 'R':
            PopulateIds (numericString);
            ReadParameters();
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

    // no ids sent, return 0
    if (strlen (labviewString) == 0)
    {
        return 0;
    }

    int16_t strLength = strlen (labviewString);
    // Add a comma at the end for consistent parsing
    labviewString[strLength] = ',';
    labviewString[strLength + 1] = NULL;

    strLength = strlen (labviewString);

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

    uint16_t count = 0;  // compared against commaCount so that the correct number of values are parsed
    char *ptr = labviewString;  // Point to the beginning of the "stripped" labview string
    char val[20];  // temp storage for value
    index = 0;
    while (count < commaCount)
    {
        // search for the next comma
        while (ptr[index] != ',')
        {
            index++;
        }
        // copy the ASCII integer into the temp ASCII
        strncpy (val, ptr, index);

        // convert from ASCII to int
        m_Id[count] = strtol (val, NULL, 10);

        // Set pointer at next char past the preceding comma
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

    if (strlen (labviewString) == 0)
    {
        return 0;
    }

    int16_t strLength = strlen (labviewString);
    // Add a comma for consistent parsing
    labviewString[strLength] = ',';
    labviewString[strLength + 1] = 0;

    strLength = strlen (labviewString);

    // Count the number of commas, the number of commas + 1 is the amount of ids and/or values sent from labview
    uint16_t index = 0;
    uint16_t commaCount = 0;

    while (index < strLength)
    {
        // search for the next comma
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

        strncpy (val, ptr, index);

        // Toggle between saving ids and vals
        if (updateId == true)
        {
            m_Id[count] = strtol (val, NULL, 10);
            updateId = false;
        }
        else
        {
            m_Value[count] = strtol (val, NULL, 10);
            updateId = true;
            count++;
        }
        // Set pointer at next char past the preceding comma
        ptr = &ptr[index + 1];
        index = 0;
    }

    // return number of id and val pairs
    return commaCount / 2;

}


uint32_t tempData[4] = {0 , 12, 345, 6789999};
void SendChannels()
{
    char outStr[WR_BUFFER_SIZE] = {NULL};
    // Add start of channel send
    strcat (outStr, "<s");
    uint16_t index = 0;
    while (m_Id[index] != -1)
    {
        char intStr[10];
        // Replace tempData with real data (tempData used for test)
        sprintf (intStr, "%ld,", tempData[m_Id[index]]);
        strcat (outStr, intStr);
        index++;
    }
    // strip the final ","
    outStr[strlen (outStr) - 1] = NULL;

    strcat (outStr, ">");
    Serial.print (outStr);

}


void WriteParameters (void)
{
    char response[100] = {NULL};;

#if 0
    // Always update both CDCs with the same info
    // Set Switch to allow the I2C bus to go to both CDC's
    Wire.beginTransmission (I2C_PCA9543A_ADDR);	// start i2c cycle
    Wire.write (CDC_BOTH);						// allow writes to go to both CDCs
    Wire.endTransmission();						// ends i2c cycle
#endif

    uint16_t index = 0;
    while (m_Id[index] != -1)
    {
		Serial.println(m_Id[index]);
		Serial.println(m_Value[index]);
        writeRegister (m_Id[index], m_Value[index]);
        index++;
    }

}

void ReadParameters (void)
{

	char response[20];

#if 0
    // Always update both CDCs with the same info
    // Set Switch to allow the I2C bus to go to both CDC's
    Wire.beginTransmission (I2C_PCA9543A_ADDR);	// start i2c cycle
    Wire.write (CDC_BOTH);						// allow writes to go to both CDCs
    Wire.endTransmission();						// ends i2c cycle
#endif
	strcpy (response, "<r");

    uint16_t index = 0;
    while (m_Id[index] != -1)
    {
		char ascii[10];
        unsigned char val = readRegister ((unsigned char)m_Id[index]);
		sprintf(ascii, "%d,", val);
		strcat(response, ascii);
        index++;
    }
	strcat(response, ">");
	Serial.print(response);

}



//////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////
//////// AD7746 Code ////////////
/////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////
void calibrate (byte direction)
{
    calibration += direction;
    // assure that calibration is in 7 bit range
    calibration &= 0x7f;
    writeRegister (REGISTER_CAP_DAC_A, _BV (7) | calibration);
}

void calibrate()
{
    calibration = 0;

    Serial.println ("Calibrating CapDAC A");

    long value = readValue();

    while (value > VALUE_UPPER_BOUND && calibration < 128)
    {
        calibration++;
        writeRegister (REGISTER_CAP_DAC_A, _BV (7) | calibration);
        value = readValue();
    }
    Serial.println ("done");
}

long readValue()
{
    long ret = 0;
    uint8_t data[3];

    char status = 0;
    //wait until a conversion is done
    while (! (status & (_BV (0) | _BV (2))))
    {
        //wait for the next conversion
        status = readRegister (REGISTER_STATUS);
    }

    unsigned long value =  readLong (REGISTER_CAP_DATA);

    value >>= 8;
    //we have read one byte to much, now we have to get rid of it
    ret =  value;

    return ret;
}


void writeRegister (unsigned char r, unsigned char v)
{
    Wire.beginTransmission (I2C_AD7746_ADDR);
    Wire.write (r);
    Wire.write (v);
    Wire.endTransmission();
}

void writeInteger (unsigned char r, unsigned int v)
{
    writeRegister (r, (unsigned byte)v);
    writeRegister (r + 1, (unsigned byte) (v >> 8));
}

unsigned char readRegister (unsigned char r)
{
    unsigned char v;
    Wire.beginTransmission (I2C_AD7746_ADDR);
    Wire.write (r);									// register to read
    Wire.endTransmission(false);

    Wire.requestFrom (I2C_AD7746_ADDR, 1);			// read a byte
    while (Wire.available() == 0)
    {
        // waiting
    }
    v = Wire.read();
    return v;
}

void readRegisters (unsigned char r, unsigned int numberOfBytes, unsigned char buffer[])
{
    unsigned char v;
    Wire.beginTransmission (I2C_AD7746_ADDR);
    Wire.write (r); // register to read
    Wire.endTransmission();

    Wire.requestFrom (I2C_AD7746_ADDR, numberOfBytes); // read a byte
    char i = 0;
    while (i < numberOfBytes)
    {
        while (!Wire.available())
        {
            // waiting
        }
        buffer[i] = Wire.read();
        i++;
    }
}

unsigned int readInteger (unsigned char r)
{
    union
    {
        char data[2];
        unsigned int value;
    }
    byteMappedInt;

    byteMappedInt.value = 0;

    Wire.beginTransmission (I2C_AD7746_ADDR); // begin read cycle
    Wire.write (0); //pointer to first cap data register
    Wire.endTransmission(); // end cycle
    //after this, the address pointer is set to 0 - since a stop condition has been sent

    Wire.requestFrom (I2C_AD7746_ADDR, r + 2); // reads 2 bytes plus all bytes before the register

    while (!Wire.available() == r + 2)
    {
        ; //wait
    }

    for (int i = r + 1; i >= 0; i--)
    {
        uint8_t c = Wire.read();
        if (i < 2)
        {
            byteMappedInt.data[i] = c;
        }
    }

    return byteMappedInt.value;

}

unsigned long readLong (unsigned char r)
{
    union
    {
        char data[4];
        unsigned long value;
    }
    byteMappedLong;

    byteMappedLong.value = 0L;

    Wire.beginTransmission (I2C_AD7746_ADDR); // begin read cycle
    Wire.write (0); //pointer to first data register
    Wire.endTransmission(); // end cycle
    //the data pointer is reset anyway - so read from 0 on

    Wire.requestFrom (I2C_AD7746_ADDR, r + 4); // reads 2 bytes plus all bytes before the register

    while (!Wire.available() == r + 4)
    {
        ; //wait
    }
    for (int i = r + 3; i >= 0; i--)
    {
        uint8_t c = Wire.read();
        if (i < 4)
        {
            byteMappedLong.data[i] = c;
        }
    }

    return byteMappedLong.value;

}

void displayStatus()
{
    unsigned char data[18];

    readRegisters (0, 18, data);

    Serial.println ("\nAD7746 Registers:");
    Serial.print ("Status (0x0): ");
    Serial.println (data[0], BIN);
    Serial.print ("Cap Data (0x1-0x3): ");
    Serial.print (data[1], BIN);
    Serial.print (".");
    Serial.print (data[2], BIN);
    Serial.print (".");
    Serial.println (data[3], BIN);
    Serial.print ("VT Data (0x4-0x6): ");
    Serial.print (data[4], BIN);
    Serial.print (".");
    Serial.print (data[5], BIN);
    Serial.print (".");
    Serial.println (data[6], BIN);
    Serial.print ("Cap Setup (0x7): ");
    Serial.println (data[7], BIN);
    Serial.print ("VT Setup (0x8): ");
    Serial.println (data[8], BIN);
    Serial.print ("EXC Setup (0x9): ");
    Serial.println (data[9], BIN);
    Serial.print ("Configuration (0xa): ");
    Serial.println (data[10], BIN);
    Serial.print ("Cap Dac A (0xb): ");
    Serial.println (data[11], BIN);
    Serial.print ("Cap Dac B (0xc): ");
    Serial.println (data[12], BIN);
    Serial.print ("Cap Offset (0xd-0xe): ");
    Serial.print (data[13], BIN);
    Serial.print (".");
    Serial.println (data[14], BIN);
    Serial.print ("Cap Gain (0xf-0x10): ");
    Serial.print (data[15], BIN);
    Serial.print (".");
    Serial.println (data[16], BIN);
    Serial.print ("Volt Gain (0x11-0x12): ");
    Serial.print (data[17], BIN);
    Serial.print (".");
    Serial.println (data[18], BIN);

}




