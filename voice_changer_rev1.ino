
// comment this out to disable debug
#define DEBUGMODE

// https://github.com/rlogiacco/CircularBuffer
#include <CircularBuffer.h>

#define BUFFERSIZE 1024
CircularBuffer<uint8_t, BUFFERSIZE> buffer;
uint16_t readIndex = 0;

// define our input and output pins
#define SAMPLE_PIN A0
#define OUTPIN 3
#define BUTTON_IN 10
#define BUTTON_GND 9
#define LED_OUT 5
#define LED_GND 6

// Define our time periods
#define ADCPERIOD 125 // how many uS to kick off ADC sample
#define DACPERIOD 250 // default uS for analog write out.  125 - 500 is an OK range?
#define BUTTONPERIOD 250000 // the button timer (250 ms)
#define RATEPERIOD 1000000 // how often to check rate potentiometer

// Variables to store timing information
unsigned long curDacPeriod = DACPERIOD;
unsigned long cur_utime = 0;
unsigned long prev_utime_adc = 0;
unsigned long prev_utime_dac = 0;
unsigned long prev_utime_button = 0;
unsigned long prev_utime_rate = 0;

// Debug values
uint16_t sMin = 1023;
uint16_t sMax = 0;
uint8_t vMin = 255;
uint8_t vMax = 0;

// variable used for averaging
uint16_t prevReading;

// control loop variables
int sampdiv = 0;
int walleMode = 0;
int highpitched = 0;
int oscillate = 0;
int button = 0;
int prevButton = 0;
int mode = 1;

// setup runs once at power up
void setup() {
  // start the serial port for debug
  Serial.begin(115200);

  // setup the ADC sample pin - this includes changing the sample clock
  pinMode(SAMPLE_PIN, INPUT);
  uint8_t regval = ADCSRA;
  regval = regval & ~(0x07);  // clear ADSPx bits
  regval = regval | 0x04; // ADSP[2:0] = 100, sets prescalar to /16
  //regval = regval | 0x05; // ADSP[2:0] = 101, sets prescalar to /32
  ADCSRA = regval;

  // setup the LED pins
  pinMode( LED_GND, OUTPUT );
  digitalWrite( LED_GND, LOW );
  pinMode( LED_OUT, OUTPUT );
  analogWrite( LED_OUT, 128 );

  // setup the button pins
  pinMode( BUTTON_GND, OUTPUT );
  digitalWrite( BUTTON_GND, LOW );
  pinMode( BUTTON_IN, INPUT );
  digitalWrite( BUTTON_IN, HIGH );  // enable internal pull up
  button = digitalRead( BUTTON_IN );  

  // debug pins
  pinMode( 11, OUTPUT );
  digitalWrite( 11, LOW );
  pinMode( 12, OUTPUT );
  digitalWrite( 12, LOW );

  // change PWM frequency for analog out D5 and D6
  // https://etechnophiles.com/change-frequency-pwm-pins-arduino-uno/
  TCCR2B = TCCR2B & B11111000 | B00000001; // for PWM frequency of 31372.55 Hz

  // initialize the buffer
  buffer.clear();
}

// loop runs repeatedly after setup is complete
void loop() {
  // get the current runtime in uS
  cur_utime = micros();

  // main input sample timer (16 kHz)
  if( cur_utime - prev_utime_adc >= ADCPERIOD ) {
    digitalWrite( 11, HIGH ); 
    prev_utime_adc = cur_utime;

    // read the ADC pin and do some signal averaging and cleanup
    uint16_t reading = analogRead(SAMPLE_PIN);
    reading = reading + 148;  // offset for 3.3 to 5V difference
    reading = 3*(prevReading/4) + reading/4;
    reading = constrain( reading, 256, 768 ); // voice falls well within this range.  This will clip noise.
    prevReading = reading;

#ifdef DEBUGMODE
    // store min and max for debug
    if( reading < sMin ) {
      sMin = reading;
    }
    if( reading > sMax ) {
      sMax = reading;
    }
#endif

    // map the input value to an output value (10 bit to 8 bit) with some gain/leveling
    uint8_t value = map( reading, 256, 768, 8, 248 );

#ifdef DEBUGMODE
    // store min and max for debug
    if( value < vMin ) {
      vMin = value;
    }
    if( value > vMax ) {
      vMax = value;
    }
#endif

    // look at what mode we are in to determine if rate should change
    // do this here so we have consistent ~16 kHz timing
    if( walleMode ) {
      if(sampdiv >= 16) {
        if(curDacPeriod <= 375)
          curDacPeriod+=1;
        else
          curDacPeriod = 125;
        sampdiv = 0;
      }
      sampdiv++;
    }

    if( oscillate ) {
      if(sampdiv >= 40) {
        if( oscillate == 1) {
          curDacPeriod+=5;
          if( curDacPeriod >= 375 ) {
            oscillate = 2;
          }
        }
        else if( oscillate == 2) {
          curDacPeriod-=5;
          if( curDacPeriod <= 125 ) {
            oscillate = 1;
          }
        }
        sampdiv = 0;
      }
      sampdiv++;
    }

    // write the output value to the buffer
    buffer.push( value );
    
    digitalWrite( 11, LOW );
  }

  cur_utime = micros();

  // output PWM timer (varies based on mode)
  if( cur_utime - prev_utime_dac >= curDacPeriod ) {
    digitalWrite( 12, HIGH ); 
    prev_utime_dac = cur_utime;

    // read in the value from the buffer
    uint16_t out = buffer[readIndex];
    readIndex++;
    if( readIndex >= BUFFERSIZE ) {
      readIndex = 0;
    }

    // set the output PWM value for the LED and speaker
    analogWrite( OUTPIN, out );
    analogWrite( LED_OUT, 128-out );
    
    digitalWrite( 12, LOW ); 
  }

  // button timer (250ms for button sampling)
  if( cur_utime - prev_utime_button >= BUTTONPERIOD ) {
    prevButton = button;
    button = digitalRead( BUTTON_IN );

    // look for a rising edge (button released)
    if( button && (prevButton == 0) ) {
      if( mode >= 4 ) {
        mode = 1;
      }
      else {
        mode++;
      }

      switch( mode ) {
        case 1:
          walleMode = 0;
          highpitched = 0;
          oscillate = 0;
          curDacPeriod = 250;
        break;

        case 2:
          walleMode = 0;
          highpitched = 1;
          oscillate = 0;
          curDacPeriod = 125;
        break;

        case 3:
          walleMode = 1;
          highpitched = 0;
          oscillate = 0;
        break;

        case 4:
          walleMode = 0;
          highpitched = 0;
          oscillate = 1;
        break;

        default:
          walleMode = 0;
          highpitched = 0;
          oscillate = 0;
        break;
          
      }
    }
  }

#ifdef DEBUGMODE
  // debug printout timer
  if( cur_utime - prev_utime_rate >= RATEPERIOD ) {
    prev_utime_rate = cur_utime;
    Serial.print( " sMin: " );
    Serial.print( sMin );
    Serial.print( " sMax: " );
    Serial.print( sMax );
    Serial.print( " vMin: " );
    Serial.print( vMin );
    Serial.print( " vMax: " );
    Serial.print( vMax );
    Serial.print( " curDacPer: " );
    Serial.print( curDacPeriod );
    Serial.println( "" );

    sMin = 1023;
    sMax = 0;
    vMin = 255;
    vMax = 0;
  }
#endif

/*
  // look for debug user entry from serial port
  while( Serial.available() > 0 ) {
    unsigned long temp = Serial.parseInt();
    if( temp == 0 ) {
      walleMode = 1;
      highpitched = 0;
      oscillate = 0;
      mode = 2;
    }
    else if( temp == 1 ) {
      highpitched = 1;
      walleMode = 0;
      oscillate = 0;
      mode = 3;
    }
    else if( temp == 2 ) {
      oscillate = 1;
      highpitched = 0;
      walleMode = 0;
      mode = 4;
    }
    else {
      curDacPeriod = temp;
      walleMode = 0;
      highpitched = 0;
      oscillate = 0;
      mode = 1;
    }
    
    if (Serial.read() == '\n') {
      if( walleMode ) {
        Serial.println( "WallE mode ACTIVATE!" );
      }
      if( highpitched ) {
        Serial.println( "now I'm squeaky!" );
      }
      if( oscillate ) {
        Serial.println( "Oscillate... or however you spell it..." );
      }
    }
  }
*/
 
}
