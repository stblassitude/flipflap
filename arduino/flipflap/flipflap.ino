#include <CoopTask.h>
#include <SPI.h>
#include <limits.h>

#define NCOLS 2
#define NROWS 1
#define DEBUG 1

byte desired_position[NCOLS][NROWS];
byte current_position[NCOLS][NROWS];

byte outputs[3];
byte input;

#if DEBUG==1
#define debug(...) Serial.printf(__VA_ARGS__);
#else
#define debug(...)
#endif

/**
 * Copies the output bits to the output shift registers.
 */
void update_outputs() {
  SPI.beginTransaction(SPISettings(32000000, MSBFIRST, SPI_MODE0));
  for (int i = 4; i--; ) {
    SPI.transfer(outputs[i]);
  }
  SPI.endTransaction();
  digitalWrite(D8, HIGH);
  delayMicroseconds(10);
  digitalWrite(D8, LOW);  
  delayMicroseconds(10);
}

/**
 * Copies the input shift register into the input bits.
 * 
 * @returns the input value
 */
byte read_input() {
  byte r;
  SPI.beginTransaction(SPISettings(32000000, MSBFIRST, SPI_MODE0));
  digitalWrite(D3, LOW);
  delayMicroseconds(10);
  digitalWrite(D3, HIGH);
  delayMicroseconds(10);
  r = SPI.transfer(0x3a);
  SPI.endTransaction();
  // There is a (hardware) bug that makes the 165 shift early, loosing the MSB, and adding a 0 at the end.
  // We can ignore the 8th bit/PBB and simply shift back down here.
  input = r >> 1;
  return input;
}

/**
 * Select one ADL.
 * 
 * @param line ADL to select, 0-7. Any other value will deselect all ADLs.
 */
void set_line(int line) {
  if (line < 0 && line > 7)
    outputs[0] = 0;
  else
    outputs[0] = 1 << line;
}

/**
 * Select one ADC.
 * 
 * @param column ADC to select, 0.14. Any other value will deselect all ADCs.
 */
void set_column(int column) {
  if (column < 0 || column > 14) {
    outputs[1] = 0;
    outputs[2] = 0;
  } else if (column < 8) {
    outputs[1] = (1 << column);
    outputs[2] = outputs[2] & 0x80;
  } else {
    outputs[1] = 0;
    outputs[2] = (outputs[2] & 0x80) | (1 << (column - 8));
  }
}

/**
 * Select all columns at once. This is mainly used to top all motors.
 */
void set_all_columns() {
  outputs[1] = 0xff;
  outputs[2] = 0x74;
}

/**
 * Set or reset the START line. When setting start to 0, will also deselect all ADLs and ADCs.
 * 
 * @param start 1 to start
 */
void set_start(int start) {
  if (start) {
    outputs[2] = outputs[2] | 0x80;
  } else {
    outputs[1] = 0;
    outputs[2] = 0;
  }
}

/**
 * Update outputs to start a single display, identified by column and line.
 * 
 * @param column the column
 * @param line the line
 */
void start_moving(int column, int line) {
  set_line(column);
  set_column(line);
  set_start(1);
  update_outputs();
  delay(10);
  set_start(0);
  update_outputs();
}

/**
 * Stop all displays on this column.
 * 
 * @param column the column
 */
void stop_col(int col) {
    set_start(0);
    update_outputs();
    delay(50);
    set_column(col);
    update_outputs();
    delay(50);
    set_start(0);
    update_outputs();
}

/**
 * Trigger t2. We wait 20ms for all motors to come to a halt.
 */
void stop_all() {
    set_start(0);
    update_outputs();
    delay(5);
    set_all_columns();
    update_outputs();
    delay(20);
}

/**
 * Update the current position of the display identified by col and row.
 * 
 * @param col the column
 * @param row the row
 */
int read_col_row(int col, int row) {
  set_column(col);
  set_line(row);
  update_outputs();
  delay(10);
  current_position[col][row] = read_input() & 0x3f;
  return current_position[col][row];
}

/**
 * Dump all the bits to the serial console.
 */
void print_state() {
  static int count = 0;

  if (count % 16 == 0) {
    Serial.println("Line____________________   Column_____________________________________  St  Data_____________  Pos");
    Serial.println("  0  1  2  3  4  5  6  7   0  1  2  3  4  5  6  7  8  9 10 11 12 13 14  rt   0  1  2  3  4  5");
  }
  for (int i = 0; i < 8; i++) {
    Serial.printf("  %c", ((outputs[0] >> i) & 1) ? 'X' : '.');
  }
  Serial.print(" ");
  for (int i = 0; i < 8; i++) {
    Serial.printf("  %c", ((outputs[1] >> i) & 1) ? 'X' : '.');
  }
  for (int i = 0; i < 7; i++) {
    Serial.printf("  %c", ((outputs[2] >> i) & 1) ? 'X' : '.');
  }
  Serial.printf("   %c ", ((outputs[2] >> 7) & 1) ? 'X' : '.');
  for (int i = 0; i < 6; i++) {
    Serial.printf("  %c", ((input >> i) & 1) ? 'X' : '.');
  }
  Serial.printf("  %2d", input & 0x3f);
  Serial.println("");
  count++;
}

/**
 * Dump the state to the serial console.
 */
void print_control_status() {
  Serial.print("Status: ");
  for (int col = 0; col < NCOLS; col++) {
    Serial.print(desired_position[col][0]);
    Serial.print(",");
  }
  Serial.print("  ");
  for (int col = 0; col < NCOLS; col++) {
    Serial.print(current_position[col][0]);
    Serial.print(",");
  }
  Serial.print("\n");
}

/**
 * Compute the waiting time for a display to reach the desired position from the current one.
 * 
 * @param col the column
 * @param row the row
 * @return absolute millis after which the motor should be stopped.
 */
long compute_deadline(int col, int row) {
  int distance = desired_position[col][row] - current_position[col][row];
  if (distance < 0)
    distance =+ 62;
  return (5500 / distance) - 10; // 5500 and 10 experimentally determined  
}

/**
 * Main control loop for starting and stopping the motors of the displays.
 */
void control_displays() {
  int deadline;
  Serial.printf("Starting control loop\n");
  stop_all();
  for (int col = 0; col < NCOLS; col++) {
    for (int row = 0; row < NROWS; row++) {
      desired_position[col][row] = 1;
      read_col_row(col, row);
    }
  }

  for (;;) {
    int nudge;
    print_control_status();
    deadline = INT_MAX; // 5500 plus margin
    for (int col = 0; col < NCOLS; col++) {
      // start motors on any display not in the correct position
      for (int row = 0; row < NROWS; row++) {
        if (desired_position[col][row] != current_position[col][row]) {
          unsigned long dl = compute_deadline(col, row);
          if (dl < deadline) {
            deadline = dl;
          }
          start_moving(col, row);
        }
      }
    }
    debug("  deadline %d\n", deadline);
    if (deadline == INT_MAX) {
      delay(100); // nothing to do, give time to other tasks
      continue;
    }

    delay(deadline);
    do {
      debug("  nudge %d\n", nudge);
      nudge = 0;
      stop_all();
      for (int col = 0; col < NCOLS; col++) {
        for (int row = 0; row < NROWS; row++) {
          if (read_col_row(col, row) == 63) {
            // not yet in reading position, quickly start it up and stop it right away again
            start_moving(col, row);
            nudge = 1;
          }
        }
      }
    } while (nudge);
    yield();
  }
}

/**
 * Accept and process input on the serial console.
 */
void console_input() {
  String s;
  for (;;) {
    delay(1);
    if (!Serial.available()) {
      yield();
      continue;
    }
    s = Serial.readString();
    s.trim();
    if (s.length() > 0) {
      int i = s.toInt();
      if (i == 0) {
        Serial.printf("Stopping...\n");
        for (int col = 0; col < NCOLS; col++) {
          for (int row = 0; row < NROWS; row++) {
            current_position[col][row] = 63;
            desired_position[col][row] = 63;
          }
        }
        stop_all();
        continue;
      }
      Serial.printf("Moving to position %d\n", i);
      for (int col = 0; col < NCOLS; col++) {
        for (int row = 0; row < NROWS; row++) {
          desired_position[col][row] = s.toInt();
        }
      }
    }
  }
}


/**
 * Blink the LED so we know the controller is still running.
 */
void heartbeat() {
  pinMode(LED_BUILTIN, OUTPUT);

  for (;;) {
    digitalWrite(LED_BUILTIN, LOW);
    delay(200);
    digitalWrite(LED_BUILTIN, HIGH);
    delay(800);
  }
}

BasicCoopTask<CoopTaskStackAllocatorAsMember<2000>> heartbeatTask("heartbeat", heartbeat);
BasicCoopTask<CoopTaskStackAllocatorAsMember<2000>> inputTask("input", console_input);
BasicCoopTask<CoopTaskStackAllocatorAsMember<2000>> controlTask("control", control_displays);

void setup() {
  Serial.begin(230400);
  SPI.begin();
  SPI.setHwCs(false);

  pinMode(D8, OUTPUT); // RCLK for output shift registers
  digitalWrite(D3, LOW); 
  pinMode(D3, OUTPUT); // /PL for input shift register
  digitalWrite(D5, HIGH); 
  memset(outputs, 0, 3);
  update_outputs();

  Serial.printf("\n\nReady\n");

  heartbeatTask.scheduleTask();
  inputTask.scheduleTask();
  controlTask.scheduleTask();
}


void loop() {
  runCoopTasks();
}
