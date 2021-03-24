#include <LiquidCrystal.h>
#include <LowPower.h>
#include <avr/sleep.h>
#include <avr/wdt.h>
#include"WearLevelling.h"

//ARDUINO PIN FUNCTIONS
#define PUMP_OUT		9
#define LCD_POWER1		7
#define LCD_POWER2		8
#define BTN_MOVE_BLACK 3
#define BTN_MOVE_WHITE 2
#define BTN_MOVE_START 4
#define LCD_RS		A0
#define LCD_ENABLE	A1
#define LCD_D4		13
#define LCD_D5		12
#define LCD_D6		11
#define LCD_D7		10

//CONSTANTS
#define DEBOUNCE_TIME 200
#define LONGPRESS_TIME 1000
#define WHITE 1
#define BLACK 0
#define GUI_UPDATE_INTERVAL_MS 300
#define SLEEP_TIMEOUT 30000

//STATES
#define GAME_STATE_INIT			0	
#define GAME_STATE_PLAYING		1
#define GAME_STATE_WAIT_START	2
#define GAME_STATE_PAUSE		3
#define GAME_STATE_END			4
#define GAME_STATE_SLEEP		5

//eeprom wear levelling library
E2WearLevelling WL;

//EEPROM stored object
struct EEPData
{
	uint32_t totalPlayedSeconds = 0;
} eepdata;

//HD44780 library
LiquidCrystal LCD(LCD_RS, LCD_ENABLE, LCD_D4, LCD_D5, LCD_D6, LCD_D7);

char lcdstr[20];
int gameState= GAME_STATE_INIT, oldGameState= GAME_STATE_INIT;
int timeEnd=0;

volatile unsigned int playerThinking=0;
volatile unsigned long playerTicks[2]={0, 0};
volatile unsigned long totalTicks=0;
volatile unsigned int gameMoves[2]={0, 0};
volatile unsigned long blackDebounceMillis=0;
volatile unsigned long whiteDebounceMillis=0;
volatile unsigned long startDebounceMillis=0;
volatile unsigned long gameDurationMinutes=5;
unsigned long idleTimer = 0;
unsigned long lastAdcMillis = 0;
uint16_t batteryVoltage = 5200;

int guiTime=0;
int pressTime=0;

//CUSTOM CHAR BITMAPS
byte batt7[] = {
  B01110,
  B11111,
  B11111,
  B11111,
  B11111,
  B11111,
  B11111,
  B11111
};

byte batt6[] = {
  B01110,
  B10001,
  B11111,
  B11111,
  B11111,
  B11111,
  B11111,
  B11111
};

byte batt5[] = {
  B01110,
  B10001,
  B10001,
  B11111,
  B11111,
  B11111,
  B11111,
  B11111
};

byte batt4[] = {
  B01110,
  B10001,
  B10001,
  B10001,
  B11111,
  B11111,
  B11111,
  B11111
};

byte batt3[] = {
  B01110,
  B10001,
  B10001,
  B10001,
  B10001,
  B11111,
  B11111,
  B11111
};

byte batt2[] = {
  B01110,
  B10001,
  B10001,
  B10001,
  B10001,
  B10001,
  B11111,
  B11111
};

byte batt1[] = {
  B01110,
  B10001,
  B10001,
  B10001,
  B10001,
  B10001,
  B10001,
  B11111
};

byte batt0[] = {
  B01110,
  B10001,
  B10001,
  B10001,
  B10001,
  B10001,
  B10001,
  B10001
};

//INTERRUPT ROUTINE CALLED BY TIMER1 OVERFLOW 100HZ
//always active
ISR(TIMER1_COMPA_vect)
{
	//decrement game time
	if((gameState== GAME_STATE_PLAYING) && (playerTicks[WHITE]==0 || playerTicks[BLACK]==0)) {oldGameState=gameState; gameState= GAME_STATE_END;}
	if(gameState== GAME_STATE_PLAYING)
	{
		playerTicks[playerThinking]--;
		totalTicks++;
	}

	//generate 100hz square wave to drive the charge pump for LCD contrast -5V
	digitalWrite(PUMP_OUT, !digitalRead(PUMP_OUT));
}

//PORT CHANGE INTERRUPT (KEY PESS)
ISR(PCINT2_vect)
{
	if(!digitalRead(BTN_MOVE_BLACK))
	{
		if(millis()-blackDebounceMillis>DEBOUNCE_TIME)
		{
			switch(gameState)
			{
			case GAME_STATE_INIT:
				gameDurationMinutes++; 
				break;
			case GAME_STATE_WAIT_START:
				playerThinking = WHITE;
				gameMoves[BLACK]++;
				oldGameState = gameState;
				gameState = GAME_STATE_PLAYING;
				break;
			case GAME_STATE_PLAYING:
				if(playerThinking==BLACK)
				{
					gameMoves[BLACK]++;
					playerThinking=WHITE;
				}
				break;
			}

		}
    blackDebounceMillis=millis();
	}
 
	if(!digitalRead(BTN_MOVE_WHITE))
	{
		if(millis()-whiteDebounceMillis>DEBOUNCE_TIME)
		{
			switch(gameState)
			{
			case GAME_STATE_INIT:
				gameDurationMinutes<=1 ? gameDurationMinutes=1 : gameDurationMinutes-- ; 
				break;
			case GAME_STATE_WAIT_START:
				playerThinking = BLACK;
				gameMoves[WHITE]++;
				oldGameState = gameState;
				gameState = GAME_STATE_PLAYING;
				break;
			case GAME_STATE_PLAYING:
				if(playerThinking==WHITE)
				{
					gameMoves[WHITE]++;
					playerThinking=BLACK;
				}
				break;
			}
		}
		whiteDebounceMillis=millis();
	}

	if(!digitalRead(BTN_MOVE_START))
	{
		if(millis()-startDebounceMillis>DEBOUNCE_TIME)
		{
			oldGameState=gameState;
			switch(gameState)
			{
				case GAME_STATE_INIT:		gameState= GAME_STATE_WAIT_START; break;
				case GAME_STATE_WAIT_START: gameState= GAME_STATE_PLAYING; break;
				case GAME_STATE_PLAYING:	gameState= GAME_STATE_PAUSE; break;
				case GAME_STATE_PAUSE:		gameState= GAME_STATE_PLAYING; break;
			}
		}
		startDebounceMillis=millis();
	}

	idleTimer = 0;
}

uint16_t readVcc()
{
	// Read 1.1V reference against AVcc
	// set the reference to Vcc and the measurement to the internal 1.1V reference
	#if defined(__AVR_ATmega32U4__) || defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__)
		ADMUX = _BV(REFS0) | _BV(MUX4) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
	#elif defined (__AVR_ATtiny24__) || defined(__AVR_ATtiny44__) || defined(__AVR_ATtiny84__)
		ADMUX = _BV(MUX5) | _BV(MUX0);
	#elif defined (__AVR_ATtiny25__) || defined(__AVR_ATtiny45__) || defined(__AVR_ATtiny85__)
		ADMUX = _BV(MUX3) | _BV(MUX2);
	#else
		ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
	#endif
	delay(100); // Wait for Vref to settle
	ADCSRA |= _BV(ADSC); // Start conversion
	while (bit_is_set(ADCSRA, ADSC)); // measuring
	uint8_t low = ADCL; // must read ADCL first - it then locks ADCH
	uint8_t high = ADCH; // unlocks both
	long result = (high << 8) | low;
	result = 1125300L / result; // Calculate Vcc (in mV); 1125300 = 1.1*1023*1000
	return result; // Vcc in millivolts
}

//render string in format 12:34
void ticksToMMSS(unsigned long ticks, char *str)
{
  unsigned long mm,ss,dd;
  mm=ticks/(60*100);
  ss=(ticks-mm*(60*100))/100;
  sprintf(lcdstr,"%02d:%02d", (int)mm, (int)ss);
}

void initAll()
{
	//LCD power supply pins
	pinMode(LCD_POWER1, OUTPUT);
	digitalWrite(LCD_POWER1, HIGH);
	pinMode(LCD_POWER2, OUTPUT);
	digitalWrite(LCD_POWER2, HIGH);

	//charge pump output
	pinMode(PUMP_OUT, OUTPUT);

	//button inputs
	pinMode(BTN_MOVE_BLACK, INPUT_PULLUP); //PCINT18
	pinMode(BTN_MOVE_WHITE, INPUT_PULLUP); //PCINT19
	pinMode(BTN_MOVE_START, INPUT_PULLUP); //PCINT20

	// Enable pin change interrupt on the PCINT18 pin using Pin Change Mask Register 2 (PCMSK2)
	PCMSK2 |= _BV(PCINT18) | _BV(PCINT19) | _BV(PCINT20);

	// Enable pin change interrupt 2 using the Pin Change Interrrupt Control Register (PCICR)
	PCICR |= _BV(PCIE2);

	Serial.begin(115200);
	delay(100);

	WL.init();
	WL.readFromEEPROM(&eepdata, sizeof(eepdata));

	//http://www.8bit-era.cz/arduino-timer-interrupts-calculator.html
	// TIMER 1 for interrupt frequency 100 Hz:
	cli(); // stop interrupts
	TCCR1A = 0; // set entire TCCR1A register to 0
	TCCR1B = 0; // same for TCCR1B
	TCNT1 = 0; // initialize counter value to 0
	// set compare match register for 100 Hz increments
	OCR1A = 9999; // = 8000000 / (8 * 100) - 1 (must be <65536)
	// turn on CTC mode
	TCCR1B |= (1 << WGM12);
	// Set CS12, CS11 and CS10 bits for 8 prescaler
	TCCR1B |= (0 << CS12) | (1 << CS11) | (0 << CS10);
	// enable timer compare interrupt
	TIMSK1 |= (1 << OCIE1A);
	sei(); // allow interrupts

	//set LCD columns and rows
	LCD.begin(24, 2);

	//loar custom chars in LCD ram
	LCD.createChar(0, batt0);
	LCD.createChar(1, batt1);
	LCD.createChar(2, batt2);
	LCD.createChar(3, batt3);
	LCD.createChar(4, batt4);
	LCD.createChar(5, batt5);
	LCD.createChar(6, batt6);
	LCD.createChar(7, batt7);
	LCD.clear();

	//recall value from EEPROM
	if (!WL.checkDataOnEEPROM(sizeof(EEPData)))
	{
		LCD.setCursor(0, 0);
		LCD.print("EEPROM err");
		WL.format();
		WL.init();
		LCD.setCursor(0, 1);
		LCD.print("FORMAT");
		eepdata.totalPlayedSeconds = 0;
		WL.writeToEEPROM(&eepdata, sizeof(eepdata));
		delay(1000);
	}
	
	LCD.setCursor(0, 0);
	LCD.print("Tot min: ");
	LCD.print(eepdata.totalPlayedSeconds/600);

	LCD.setCursor(0, 1);
	LCD.print("Batt mV: ");
	batteryVoltage = readVcc();
	LCD.print(batteryVoltage);

	delay(1000);
	playerTicks[0] = gameDurationMinutes * 60 * 100;
	playerTicks[1] = gameDurationMinutes * 60 * 100;
	
}

void animateArrowL(int GUITicks, char* out)
{
	switch (GUITicks % 3)
	{
	case 0: sprintf(out, "  <"); break;
	case 1: sprintf(out, " <<"); break;
	case 2: sprintf(out, "<<<"); break;
	}
}

void animateArrowR(int GUITicks, char* out)
{
	switch (GUITicks % 3)
	{
	case 0: sprintf(out, ">  "); break;
	case 1: sprintf(out, ">> "); break;
	case 2: sprintf(out, ">>>"); break;
	}
}

long mapLim(long x, long in_min, long in_max, long out_min, long out_max)
{
	if (x > in_max) return out_max;
	if (x < in_min) return out_min;
	return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

//arduino system function: called at power-up
void setup() 
{
	initAll();
}

//arduino main loop
void loop() 
{
    char arrowAniStr[8];
    char gameStatusString[8];
    //set game duration
    if(gameState==GAME_STATE_INIT)
    {
      sprintf(lcdstr,"+ IMPOSTA DUR. -");
	  totalTicks = 0;
      playerTicks[0]=gameDurationMinutes*60*100;
      playerTicks[1]=gameDurationMinutes*60*100;
      gameMoves[1]=0;
      gameMoves[0]=0;
	  idleTimer += GUI_UPDATE_INTERVAL_MS;
    }    
	if (gameState == GAME_STATE_WAIT_START)
	{
		sprintf(lcdstr, "< PRIMA  MOSSA >", gameStatusString);
	}
	if(gameState== GAME_STATE_PLAYING)
    {
       sprintf(gameStatusString,"MUOVI ");
	   guiTime++;
	   idleTimer=0;
    }
    if(gameState== GAME_STATE_PAUSE)
    {
       sprintf(gameStatusString,"PAUSA ");
	   guiTime = 2;
	   idleTimer += GUI_UPDATE_INTERVAL_MS;
    }
    if(gameState== GAME_STATE_END)
    {
       sprintf(gameStatusString,"FINE! ");
	   guiTime = 2;
	   eepdata.totalPlayedSeconds += totalTicks/100;
	   WL.writeToEEPROM(&eepdata, sizeof(eepdata));
	   idleTimer += GUI_UPDATE_INTERVAL_MS;
    }    

	if (gameState == GAME_STATE_SLEEP)
	{
		LCD.clear();
		//turn off LCD
		digitalWrite(LCD_POWER1, LOW);
		digitalWrite(LCD_POWER2, LOW);

		//turn off charge pump
		digitalWrite(PUMP_OUT, LOW);

		//set pins as input
		pinMode(LCD_POWER1, INPUT);
		pinMode(LCD_POWER2, INPUT);
		pinMode(PUMP_OUT,   INPUT);

		pinMode(BTN_MOVE_BLACK, OUTPUT); //PCINT18
		pinMode(BTN_MOVE_WHITE, OUTPUT); //PCINT19
		digitalWrite(BTN_MOVE_BLACK, LOW);
		digitalWrite(BTN_MOVE_WHITE, LOW);
		idleTimer = 0;
		delay(200);

		//HALT CPU
		LowPower.powerDown(SLEEP_FOREVER, ADC_OFF, BOD_OFF);

		//wakeup starts from here
		gameState = GAME_STATE_INIT;
		initAll();
	}

    //reset game
    if(oldGameState!= GAME_STATE_INIT && gameState== GAME_STATE_INIT)
    {
        totalTicks=0;
        playerTicks[0]=gameDurationMinutes*60*100;
        playerTicks[1]=gameDurationMinutes*60*100;
        gameMoves[1]=0;
        gameMoves[0]=0;
    }

     if(gameState== GAME_STATE_PLAYING || gameState== GAME_STATE_PAUSE || gameState== GAME_STATE_END)
    {
		if(playerThinking==BLACK)
		{
			animateArrowL(guiTime,arrowAniStr);
			sprintf(lcdstr,"%s  %s      ",arrowAniStr, gameStatusString);
		}
		else
		{
			animateArrowR(guiTime,arrowAniStr);
			sprintf(lcdstr,"     %s  %s",gameStatusString, arrowAniStr);
		}
    }

	if (idleTimer >= SLEEP_TIMEOUT) gameState = GAME_STATE_SLEEP;

    if(!digitalRead(BTN_MOVE_START)) pressTime+=GUI_UPDATE_INTERVAL_MS;
    else pressTime=0;    
    if(pressTime>LONGPRESS_TIME)
    {
      startDebounceMillis=millis();
      gameState= GAME_STATE_INIT;
    }

	if (millis() - lastAdcMillis > 30000)
	{
		lastAdcMillis = millis();
		batteryVoltage = readVcc();
	}

	//PRINT GAME STATE
	LCD.setCursor(0, 0);
	LCD.print(lcdstr);

    //PRINT GAME MOVES
	LCD.setCursor(0, 1);
    sprintf(lcdstr,"%-3dM        M%3d",gameMoves[0],gameMoves[1]);
    LCD.print(lcdstr);

	//PRINT battery
	LCD.setCursor(8, 1);
	int battLevel = mapLim((long)batteryVoltage, 2800, 3400, 1, 7);
	int battLevelP = mapLim((long)batteryVoltage, 2800, 3400, 0, 100);
	LCD.write((byte)battLevel);
	
    //PRINT TIME
    LCD.setCursor(16, 0);
    ticksToMMSS(playerTicks[0],lcdstr);
    LCD.print(lcdstr);
    LCD.setCursor(19, 1);
    ticksToMMSS(playerTicks[1],lcdstr);
    LCD.print(lcdstr);

	delay(GUI_UPDATE_INTERVAL_MS);
}
