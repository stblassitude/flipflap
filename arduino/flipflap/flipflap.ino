#include <CoopTask.h>
#include <SPI.h>
#include <limits.h>

#define NCOLS 2
#define NROWS 1
#define DEBUG 1

volatile byte desired_position[NCOLS][NROWS];
volatile byte current_position[NCOLS][NROWS];

/**
 * Maps ISO-8859-1 characters (char) to their position on the display (index). Of the 64 encodings, 0 and 63 are not used; they are encoded as \0.
 */
const char lookup[64] = "\000ABCDEFGHIJKLMNOPQRSTUVWXYZ ;=()?!,-./0123456789:\xd6\xc4\xd8\xdc\000";

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
 * Trigger motor stop. We wait half a half-cycle for all motors to come to a halt.
 */
void stop_all() {
    select_none();
    update_outputs();
    delay(50);
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
  int p = 63;
  int matches = 5; // a number of consecutive identical reads should be reliable
  for (int i = 10; i--; ) {
    select_none();
    update_outputs();
    delay(2);
    select_column(col);
    select_line(row);
    update_outputs();
    delay(5);
    current_position[col][row] = read_input() & 0x3f;
    if (current_position[col][row] != 63 && current_position[col][row] == p)
      matches--;
    if (matches <= 0)
      break;
    p = current_position[col][row];
  }
  return current_position[col][row];
}

int char_to_position(char c) {
  for (int i = 64; i--; ) {
    if (c == lookup[i])
      return i ^ 0x3f;
  }
  return 63;
}

char position_to_char(int p) {
  if (p == 0 || p == 63)
    return '_';
  return lookup[p ^ 0x3f];
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
      int p = desired_position[col][0];
      Serial.printf("%c (%3d), ", position_to_char(p), p);
    }
  }
  Serial.print("\nCurrent: ");
  for (int col = 0; col < NCOLS; col++) {
    for (int row = 0; row < NROWS; row++) {
      int p = current_position[col][0];
      Serial.printf("%c (%3d), ", position_to_char(p), p);
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
  int distance = current_position[col][row] - desired_position[col][row];
  if (distance < 0)
    distance += 62;
    debug("  distance %d/%d = %d-%d = %d\n", col, row, current_position[col][row], desired_position[col][row], distance);
  return (4500 * distance / 62) - 50; // experimentally determined
}

void nudge(int col, int row) {
  select_none();
  update_outputs();
  select_column(col);
  select_line(row);
  set_start();
  update_outputs();
  delay(5);
  select_none();
  set_start();
  update_outputs();
}

/**
 * Main control loop for starting and stopping the motors of the displays.
 */
void control_displays() {
  int deadline;
  Serial.printf("Starting control loop\n");
  stop_all();
  int space = char_to_position(' ');
  for (int col = 0; col < NCOLS; col++) {
    for (int row = 0; row < NROWS; row++) {
      desired_position[col][row] = space;
      read_col_row(col, row);
    }
  }

  for (;;) {
    int running;

    yield();
    deadline = INT_MAX;
    running = 0;
    for (int col = 0; col < NCOLS; col++) {
      // start motors on any display not in the correct position
      for (int row = 0; row < NROWS; row++) {
        if (desired_position[col][row] != 63 && desired_position[col][row] != current_position[col][row]) {
          int dl = compute_deadline(col, row);
          if (dl < deadline) {
            deadline = dl;
          }
          debug("  starting %d/%d, dl %d\n", col, row, dl);
          select_none();
          update_outputs();
          select_column(col);
          select_line(row);
          delay(5);
          set_start();
          update_outputs();
          delay(5);
          select_none();
          set_start();
          update_outputs();
          running = 1;
        }
      }
    }
    if (!running) {
      delay(1000); // nothing to do, give time to other tasks
      continue;
    }
    print_control_status();

    debug("  deadline %d\n", deadline);
    delay(deadline);
    do {
      running = 0;
      debug("  reading\n");
      stop_all();
      for (int col = 0; col < NCOLS; col++) {
        for (int row = 0; row < NROWS; row++) {
          read_col_row(col, row);
          if (desired_position[col][row] != 63 && current_position[col][row] == 63)
            running = 1;
        }
      }
      print_control_status();
      select_none();
      if (running) {
        for (int col = 0; col < NCOLS; col++) {
          for (int row = 0; row < NROWS; row++) {
            if (desired_position[col][row] != 63 && current_position[col][row] == 63) {
              // not yet in reading position, quickly start it up and stop it right away again
              debug("  nudge %d/%d\n", col, row);
              nudge(col, row);
              print_control_status();
            }
          }
        }
      }
    } while (running);
    debug("  done nudging\n");
    print_control_status();
    // for (int col = 0; col < NCOLS; col++) {
    //   for (int row = 0; row < NROWS; row++) {
    //     desired_position[col][row] = 63;
    //   }
    // }
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
    if (s.length() > 0) {
      if (s.equals("^")) {
        Serial.printf("Stopping...\n");
        for (int col = 0; col < NCOLS; col++) {
          for (int row = 0; row < NROWS; row++) {
            desired_position[col][row] = 63;
          }
        }
        stop_all();
        continue;
      }

      int space = char_to_position(' ');
      int i = 0;
      for (int col = 0; col < NCOLS; col++) {
        for (int row = 0; row < NROWS; row++) {
          desired_position[col][row] = space;
        }
      }
      s.toUpperCase();
      Serial.printf("Updating \"%s\"\n", s);
      for (int col = 0; col < NCOLS && i < s.length(); col++) {
        for (int row = 0; row < NROWS && i < s.length(); row++) {
          int p;
          do {
            p = char_to_position(s.charAt(i++));
          } while (p <= 0 && p >= 63 && i < s.length());
          if (p != 0 && p != 63) {
            desired_position[col][row] = p;
          }
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

void nudgetest() {
  for (;;) {
    if (Serial.available()) {
      Serial.read();
      Serial.printf("\nnudge\n");
      nudge(0, 0);
    }
    read_col_row(0, 0);
    Serial.printf("position %3d\n", current_position[0][0]);
    delay(1000);
  }
}

void setup() {
  Serial.begin(230400);
  SPI.begin();
  SPI.setHwCs(false);

  pinMode(D8, OUTPUT); // RCLK for output shift registers
  digitalWrite(D3, LOW); 
  pinMode(D3, OUTPUT); // /PL for input shift register
  digitalWrite(D5, HIGH); 
  
  for (int i = 10; i--; )
    stop_all();

  // Serial.printf("\n\nWaiting for keypress to start...");
  // while(!Serial.available())
  //   delay(10);
  // Serial.read();

  // nudgetest();

  Serial.printf("\n\nReady\n");

  heartbeatTask.scheduleTask();
  inputTask.scheduleTask();
  controlTask.scheduleTask();
}


void loop() {
  runCoopTasks();
}
