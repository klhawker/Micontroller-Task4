#include "mbed.h"
#include "C12832.h"

#define NUM_STATES 4

// Define the pins for the LEDs, joystick, fire button and LCD
DigitalOut led1(D5);
DigitalOut led2(D8);
DigitalOut led3(D9);
InterruptIn fire(D4);
InterruptIn joystickUp(A2); 
InterruptIn joystickDown(A3); 
C12832 lcd(D11, D13, D12, D7, D10);
Ticker clockTimer;

// Serial monitor for debugging
Serial pc(USBTX, USBRX);

// Enumeration for different states
typedef enum {
    SetTime,
    DisplayCurrentTime,
    StopwatchRunning,
    StopwatchPaused
} State;

// Class definition for Potentiometer
class Potentiometer {
private:                                           
    AnalogIn inputSignal;                           
    float VDD, currentSampleNorm, currentSampleVolts; 

public:                                             
    Potentiometer(PinName pin, float v) : inputSignal(pin), VDD(v) {}

    float amplitudeVolts(void) {
        return (inputSignal.read() * VDD);
    }
    
    float amplitudeNorm(void) {
        return inputSignal.read();
    }
    
    void sample(void) {
        currentSampleNorm = inputSignal.read();
        currentSampleVolts = currentSampleNorm * VDD;
    }
    
    float getCurrentSampleVolts(void) {
        return currentSampleVolts;
    }
    
    float getCurrentSampleNorm(void) {
        return currentSampleNorm; 
    }
};

class SamplingPotentiometer : public Potentiometer {
private: 
    float samplingFrequency, samplingPeriod;
    Ticker sampler;

    static void isr(SamplingPotentiometer* obj) {
        obj->sample();
    }

public: 
    SamplingPotentiometer(PinName p, float v, float fs) : Potentiometer(p, v), samplingFrequency(fs) {
        samplingPeriod = 1.0f / samplingFrequency;
    }

    void startSampling() {
        sampler.attach(callback(SamplingPotentiometer::isr, this), samplingPeriod);
    }

    void stopSampling() {
        sampler.detach();
    }
};

class Clock {
private:
    int hours, minutes, seconds;

public:
    Clock() : hours(0), minutes(0), seconds(0) {}

    void tick() {
        seconds++;
        if (seconds >= 60) {
            seconds = 0;
            minutes++;
            if (minutes >= 60) {
                minutes = 0;
                hours = (hours + 1) % 24;
            }
        }
    }

    void setTime(int h, int m, int s) {
        hours = h;
        minutes = m;
        seconds = s;
    }

    int getHours() const { return hours; }
    int getMinutes() const { return minutes; }
    int getSeconds() const { return seconds; }
};

class LED {                                 // RGB LED class                                    
private:
    DigitalOut red, green, blue;                                                            
    char state;
public:
    LED(PinName r, PinName g, PinName b) : red(r), green(g), blue(b){  
        Off();                                                  
    }                                                                  

    void Green(int g){
        red = g;                                    // active low so on = 0, hence why !g
        green = !g;                    
        blue = g;
    }

    void Blue(int b){
        red = b;
        green = b;
        blue = !b;
    }

    void Off(void) {                      
        red = 1;                            
        green = 1;                          
        blue = 1;                          
        state = 0;                          
    }

};

class Speaker { 
    private:
        DigitalOut outputSignal;
        char state; // Can be set to either 1 or 0 to record output value public:
    public:
        Speaker(PinName pin) : outputSignal(pin){}
      
        void on(void){
            state = 1;
            outputSignal = state;
        };
        void off(void){
            state = 0;
            outputSignal = state;
        }
        void toggle(void){
            state = !state;
            outputSignal = state;
        }
};

// Global variables
State current_state = SetTime;
bool entered_state = true;

int setHours = 0;  // Global variable for set hours
int setMinutes = 0;  // Global variable for set minutes
int seconds = 0;  // Global variable for seconds

int prevSetHours = -1;  // Initialize to an invalid value
int prevSetMinutes = -1;  // Initialize to an invalid value

SamplingPotentiometer pot1(A0, 23.0f, 10.0f);
SamplingPotentiometer pot2(A1, 60.0f, 10.0f);
Clock myClock;
LED led(D5, D9, D8); 
Speaker mySpeaker(D6);

Timer stopwatchTimer;
int stopwatchSeconds = 0;

void updateStopwatch() {
    stopwatchSeconds = stopwatchTimer.read();
}

void resetStopwatch() {
    stopwatchTimer.reset();
    stopwatchSeconds = 0;
}

void updateLEDs() {
    if (current_state == StopwatchRunning) {
        // Turn on LED to indicate running
        led.Blue(1);
    } else {
        // Turn off LED to indicate paused
        led.Off();
    }
}


// Interrupt handlers
void onUp() {
    // Move to the next state
    current_state = static_cast<State>((current_state + 1) % NUM_STATES);
    entered_state = true; // Reset the entered_state flag
}

void onDown() {
    // Move to the previous state
    current_state = static_cast<State>((current_state + NUM_STATES - 1) % NUM_STATES);
    entered_state = true; // Reset the entered_state flag
}


// Handler for fire button press
void onFire() {
    if (current_state == SetTime) {
        myClock.setTime(setHours, setMinutes, 0);  // Initialize clock with the set time
        current_state = DisplayCurrentTime;
    }
    if (current_state == StopwatchRunning) {
        stopwatchTimer.stop();
        current_state = StopwatchPaused;
    } else if (current_state == StopwatchPaused) {
        stopwatchTimer.start();
        current_state = StopwatchRunning;
}
}

void tick(){
    myClock.tick();
}

int main() {
    joystickUp.rise(&onUp);
    joystickDown.rise(&onDown);
    fire.rise(&onFire);

    pot1.startSampling();
    pot2.startSampling();
    pc.printf("Serial is working\n");
    clockTimer.attach(&tick, 1.0);

    while (true) {
        // State machine logic
        switch (current_state) {
            case SetTime:
                pc.printf("SetTime");
                // Read potentiometer values to set the time
                int currentHours = static_cast<int>((pot1.getCurrentSampleVolts() / 23.0f) * 24) % 24;  // Ensure hours wrap around after 23
                int currentMinutes = static_cast<int>((pot2.getCurrentSampleVolts() / 60.0f) * 60) % 60;  // Ensure minutes wrap around after 59
                if (entered_state) {
                    entered_state = false;
                    // Reset the set time to 0:00 if you want to start fresh each time you enter this state
                    setHours = 0;
                    setMinutes = 0;
                }
                
            // Update the LCD only if the potentiometer values have changed
                if (currentHours != prevSetHours || currentMinutes != prevSetMinutes) {
                    setHours = currentHours;
                    setMinutes = currentMinutes;

                    // Update the LCD with the new time
                    lcd.cls();
                    lcd.locate(20, 10);
                    lcd.printf("Set Time: %02d:%02d", setHours, setMinutes);

                    // Update the previous values
                    prevSetHours = currentHours;
                    prevSetMinutes = currentMinutes;
                }
                break;

            case DisplayCurrentTime:
                pc.printf("DisplayCurrentTime\n");
                if (entered_state) {
                    // Reset entered_state when entering the state
                    entered_state = false;
                }
                // Display the current time
                lcd.cls();
                lcd.locate(20, 10);
                lcd.printf("Time: %02d:%02d:%02d", myClock.getHours(), myClock.getMinutes(), myClock.getSeconds());
                break;
            case StopwatchRunning:
                pc.printf("StopwatchRunning\n");
                if (entered_state) {
                    lcd.cls();
                    lcd.locate(20, 10);
                    lcd.printf("Stopwatch Running");
                    stopwatchTimer.start();
                    entered_state = false;
                }
                updateStopwatch();
                // Display stopwatch time
                lcd.locate(20, 20);
                lcd.printf("%02d s", stopwatchSeconds);
                break;

            case StopwatchPaused:
            pc.printf("StopwatchPaused\n");
                if (entered_state) {
                    lcd.cls();
                    lcd.locate(20, 10);
                    lcd.printf("Stopwatch Paused");
                    entered_state = false;
                }
                // Display current stopwatch time
                lcd.locate(20, 20);
                lcd.printf("%02d s", stopwatchSeconds);
                updateLEDs();
                break;

        }

        wait_ms(100); // Wait for 100 milliseconds
    }
}
