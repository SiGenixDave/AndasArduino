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
#define CDC_1						0x01  // select CDC 1 (to perform register reads)
#define CDC_2						0x02  // select CDC 2 (to perform register reads)
#define CDC_BOTH					0x03  // select both CDCs (identical writes to both)

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
static volatile uint32_t m_TimerCounter;	// Reported back to UI;

static bool m_Sample;						// when true and m_cycling true, ADC sampling occurs
static bool m_Blink;						// when true blinking occurs
static uint16_t m_SampleTimeMsecs = 50;		// default sample time; can be changed by Windows app

byte calibration;
byte outOfRangeCount = 0;
unsigned long offset = 0;
uint32_t capValue[4];
/*--------------------------------------------------------------------------
 MODULE PROTOTYPES
 --------------------------------------------------------------------------*/

// the setup routine runs once when power is applied or reset is  pressed
void setup()
{
    char stat = 0;
    char capdac_a = 0;
    
    // setup serial port to intercept PC commands
    Serial.begin (115200);

    while (!Serial) {}

    Serial.println ("hello world");

    Wire.begin();

    // Set Switch to allow the I2C bus to go to both CDC's
    SelectCDC(CDC_BOTH);

    // Reset both CDC's
    Wire.beginTransmission (I2C_AD7746_ADDR);	// start i2c cycle
    Wire.write (RESET_ADDRESS);					// reset the CDCs
    Wire.endTransmission();						// ends i2c cycle

    // wait for reboot
    delay (1);

    writeRegister (REGISTER_EXC_SETUP, 0x18); // EXC source A+B; VDD/8 excitation voltage
    writeRegister (REGISTER_CAP_SETUP, 0x80); // cap setup reg - cap enabled
    writeRegister (REGISTER_CONFIGURATION, 0x20); // configuration register - 62ms update rate
    // **TRS 3-17 write to CAPDAC register using formula from sheet 3 of app note CN-1029
    writeRegister (REGISTER_CAP_DAC_A, 0x51); // CAPDAC_A - TEST    

    // Set Switch to connect the I2C bus to CDC 1
    SelectCDC(CDC_1);

    Serial.println ("\nCDC #1");
    Serial.println ("---------------");
    
    displayStatus();  // register dump
    
    // Set Switch to connect the I2C bus to CDC 2
    SelectCDC(CDC_2);

    Serial.println ("\nCDC #2");
    Serial.println ("---------------");
    
    displayStatus();  // register dump

    Serial.println ("\ndone");
}



// the loop routine runs over and over again forever:
void loop()
{
    ProcessSerialInput();
	ReadCapValues();
}

void SelectCDC (unsigned char cdc)
{
    Wire.beginTransmission (I2C_PCA9543A_ADDR);	// start i2c cycle
    Wire.write (cdc);						    // allow writes to go to one or both CDCs
    Wire.endTransmission();						// ends i2c cycle
    Wire.endTransmission();						// **TRS 3-17 second stop condition
}

//uint32_t capValue[4];
void ReadCapValues (void)
{
    SelectCDC (CDC_BOTH);

	// Setup config registers for channel 2 on both CDCs
//	writeRegister (0x0b, 0xb8);
	writeRegister (0x07, 0xc0);
//	writeRegister (0x09, 0x23);
	writeRegister (0x0A, 0x21);
	delay (100);

    SelectCDC (CDC_1);						    
	capValue[0] = readValue();

    SelectCDC (CDC_2);						    
	capValue[2] = readValue();

	//////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////
    SelectCDC (CDC_BOTH);

	// Setup config registers for channel 1 on both CDCs
//	writeRegister (0x0b, 0xb8);
	writeRegister (0x07, 0x80);
//	writeRegister (0x09, 0x0b);
	writeRegister (0x0A, 0x21);
	delay (100);

    SelectCDC (CDC_1);						    
	capValue[1] = readValue();

    SelectCDC (CDC_2);						    
	capValue[3] = readValue();

	
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


void SendChannels()
{
    char outStr[WR_BUFFER_SIZE] = {NULL};
    // Add start of channel send
    strcat (outStr, "<s,");
    uint16_t index = 0;
    while (m_Id[index] != -1)
    {
        char intStr[10];
        // Replace tempData with real data (tempData used for test)
        sprintf (intStr, "%ld,", capValue[m_Id[index]]);
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

    SelectCDC (CDC_BOTH);						// allow writes to go to both CDCs

    uint16_t index = 0;
	strcpy(response, "<w");
    while (m_Id[index] != -1)
    {
		char val[20];
		sprintf(val, "%d,", m_Id[index]);
		strcat(response, val);
        writeRegister (m_Id[index], m_Value[index]);
        index++;
    }

    // strip the final ","
    response[strlen (response) - 1] = NULL;
	strcat(response, ">");

	Serial.print(response);

}

void ReadParameters (void)
{

	char response[100];

    SelectCDC (CDC_BOTH);						// allow writes to go to both CDCs

	strcpy (response, "<r");

    uint16_t index = 0;
    while (m_Id[index] != -1)
    {
		char ascii[10];
		unsigned char cdcReg = SwitchCDC ((unsigned char)m_Id[index]);
        unsigned char val = readRegister (cdcReg);
		sprintf(ascii, "%d,", val);
		strcat(response, ascii);
        index++;
    }
    // strip the final ","
    response[strlen (response) - 1] = NULL;

	strcat(response, ">");
	Serial.print(response);

}

unsigned char SwitchCDC (unsigned char regId)
{
	if (regId > 100)
	{
		regId -= 100;

	    // Set Switch to allow the I2C bus to go to both CDC's
		Wire.beginTransmission (I2C_PCA9543A_ADDR);	// start i2c cycle
		Wire.write (CDC_2);							// allow writes to go to CDC 1
		Wire.endTransmission();						// ends i2c cycle
	}
	else
	{
	    // Set Switch to allow the I2C bus to go to both CDC's
		Wire.beginTransmission (I2C_PCA9543A_ADDR);	// start i2c cycle
		Wire.write (CDC_1);							// allow writes to go to CDC 1
		Wire.endTransmission();						// ends i2c cycle
	}
	Wire.endTransmission();						// **TRS 3-17 add second stop condition

	return regId;

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
    Wire.endTransmission(false);

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

    Serial.println ("AD7746 Registers:");
    Serial.print ("Status (0x0): 0x");
    if (data[0]<0x10) {Serial.print("0");}
    Serial.println (data[0], HEX);
    Serial.print ("Cap Data (0x1-0x3): 0x");
    if (data[1]<0x10) {Serial.print("0");}
    Serial.print (data[1], HEX);
    if (data[2]<0x10) {Serial.print("0");}
    Serial.print (data[2], HEX);
    if (data[3]<0x10) {Serial.print("0");}
    Serial.println (data[3], HEX);
    Serial.print ("VT Data (0x4-0x6): 0x");
    if (data[4]<0x10) {Serial.print("0");}
    Serial.print (data[4], HEX);
    if (data[5]<0x10) {Serial.print("0");}
    Serial.print (data[5], HEX);
    if (data[6]<0x10) {Serial.print("0");}
    Serial.println (data[6], HEX);
    Serial.print ("Cap Setup (0x7): 0x");
    if (data[7]<0x10) {Serial.print("0");}
    Serial.println (data[7], HEX);
    Serial.print ("VT Setup (0x8): 0x");
    if (data[8]<0x10) {Serial.print("0");}
    Serial.println (data[8], HEX);
    Serial.print ("EXC Setup (0x9): 0x");
    if (data[9]<0x10) {Serial.print("0");}
    Serial.println (data[9], HEX);
    Serial.print ("Configuration (0xa): 0x");
    if (data[10]<0x10) {Serial.print("0");}
    Serial.println (data[10], HEX);
    Serial.print ("Cap Dac A (0xb): 0x");
    if (data[11]<0x10) {Serial.print("0");}
    Serial.println (data[11], HEX);
    Serial.print ("Cap Dac B (0xc): 0x");
    if (data[12]<0x10) {Serial.print("0");}
    Serial.println (data[12], HEX);
    Serial.print ("Cap Offset (0xd-0xe): 0x");
    if (data[13]<0x10) {Serial.print("0");}
    Serial.print (data[13], HEX);
    if (data[14]<0x10) {Serial.print("0");}
    Serial.println (data[14], HEX);
    Serial.print ("Cap Gain (0xf-0x10): 0x");
    if (data[15]<0x10) {Serial.print("0");}
    Serial.print (data[15], HEX);
    if (data[16]<0x10) {Serial.print("0");}
    Serial.println (data[16], HEX);
    Serial.print ("Volt Gain (0x11-0x12): 0x");
    if (data[17]<0x10) {Serial.print("0");}
    Serial.print (data[17], HEX);
    if (data[18]<0x10) {Serial.print("0");}
    Serial.println (data[18], HEX);

}



