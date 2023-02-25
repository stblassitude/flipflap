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
 * De-selects all columns, lines, and start.
 */
void select_none() {
  outputs[0] = 0;
  outputs[1] = 0;
  outputs[2] = 0;
}

/**
 * Select one ADL.
 * 
 * @param line ADL to select, 0-7.
 */
void select_line(int line) {
  if (line >= 0 && line <= 7)
    outputs[0] |= 1 << line;
}

/**
 * Select one ADC.
 * 
 * @param column ADC to select.
 */
void select_column(int column) {
  if (column < 0 || column > 14) {
    return;
  } else if (column < 8) {
    outputs[1] |= 1 << column;
  } else {
    outputs[2] |= 1 << (column - 8);
  }
}

/**
 * Select all columns at once. This is mainly used to top all motors.
 */
void select_all_columns() {
  outputs[1] = 0xff;
  outputs[2] = 0x7f;
}

/**
 * Returns true if at least one line or column is selected
 * 
 * @returns true if any line or column is selected
 */
int any_selected() {
  return outputs[0] || outputs[1] || outputs[2];
}

/**
 * Set the START line.
 * 
 * @param start 1 to start
 */
void set_start() {
  outputs[2] = outputs[2] | 0x80;
}


/**
 * Trigger t2. We wait half a half-cycle for all motors to come to a halt.
 */
void stop_all() {
    select_none();
    update_outputs();
    delay(5);
    select_all_columns();
    update_outputs();
    delay(20);
    select_none();
    update_outputs();
}

/**
 * Update the current position of the display identified by col and row.
 * 
 * @param col the column
 * @param row the row
 */
int read_col_row(int col, int row) {
  select_none();
  select_column(col);
  select_line(row);
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
  Serial.print("Desired: ");
  for (int col = 0; col < NCOLS; col++) {
    for (int row = 0; row < NROWS; row++) {
      Serial.printf("%3d, ", desired_position[col][0]);
    }
  }
  Serial.print("\nCurrent: ");
  for (int col = 0; col < NCOLS; col++) {
    for (int row = 0; row < NROWS; row++) {
      Serial.printf("%3d, ", current_position[col][0]);
    }
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
  return (5000 / distance) - 10; // 5000 and 10 experimentally determined  
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
      desired_position[col][row] = 63;
      read_col_row(col, row);
    }
  }

  for (;;) {
    int nudge;

    yield();
    print_control_status();
    select_none();
    update_outputs();
    deadline = INT_MAX;
    for (int col = 0; col < NCOLS; col++) {
      // start motors on any display not in the correct position
      for (int row = 0; row < NROWS; row++) {
        if (desired_position[col][row] != 63 && desired_position[col][row] != current_position[col][row]) {
          unsigned long dl = compute_deadline(col, row);
          if (dl < deadline) {
            deadline = dl;
          }
          select_column(col);
          select_line(row);
        }
      }
    }
    if (!any_selected()) {
      delay(1000); // nothing to do, give time to other tasks
      continue;
    }
    delay(5); // wait for all selects to disengage
    set_start();
    update_outputs();
    delay(5);
    select_none();
    update_outputs();

    debug("  deadline %d\n", deadline);
    delay(deadline);
    do {
      nudge = 0;
      stop_all();
      for (int col = 0; col < NCOLS; col++) {
        for (int row = 0; row < NROWS; row++) {
          read_col_row(col, row);
          if (desired_position[col][row] != 63 && current_position[col][row] == 63)
            nudge = 1;
        }
      }
      debug("  nudge %d\n", nudge);
      select_none();
      if (nudge) {
        for (int col = 0; col < NCOLS; col++) {
          for (int row = 0; row < NROWS; row++) {
            if (desired_position[col][row] != 63 && current_position[col][row] == 63) {
              // not yet in reading position, quickly start it up and stop it right away again
              select_column(col);
              select_line(row);
            }
          }
        }
        print_control_status();
        set_start();
        update_outputs();
        delay(5);
        select_none();
        update_outputs();
        delay(10); // half of a half-cycle for the motors
      }
    } while (nudge);
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
  
  select_none();
  update_outputs();
  // delay(1000);
  for (int i = 10; i--; )
    stop_all();

  Serial.printf("\n\nWaiting for keypress to start...");
  while(!Serial.available())
    delay(10);
  Serial.read();

  Serial.printf("\n\nReady\n");

  heartbeatTask.scheduleTask();
  inputTask.scheduleTask();
  controlTask.scheduleTask();
}


void loop() {
  runCoopTasks();
}
