/*
 *
 * CSEE 4840 Lab 2 for 2019
 *
 * Name/UNI: Please Changeto Yourname (pcy2301)
 */
#include "fbputchar.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "usbkeyboard.h"
#include <pthread.h>

/* Update SERVER_HOST to be the IP address of
 * the chat server you are connecting to
 */
/* arthur.cs.columbia.edu */
#define SERVER_HOST "128.59.19.114"
#define SERVER_PORT 42000

#define BUFFER_SIZE 128

//#define USB_LSHIFT 0x02
//#define USB_RSHIFT 0x20

#define INPUT_FIRST_ROW 21
#define INPUT_SECOND_ROW 22
#define MAX_COLS 64
#define CURSOR_CHAR '|'

#define LEFT_KEY -2
#define RIGHT_KEY -3

/*
 * References:
 *
 * https://web.archive.org/web/20130307100215/http://beej.us/guide/bgnet/output/html/singlepage/bgnet.html
 *
 * http://www.thegeekstuff.com/2011/12/c-socket-programming/
 * 
 */

int sockfd; /* Socket file descriptor */

struct libusb_device_handle *keyboard;
uint8_t endpoint_address;

pthread_t network_thread;
void *network_thread_f(void *);

int main()
{
  int err, col;

  /////////////////////added variable
  int rows;
  int key;

  char input_buffer[BUFFER_SIZE];
  int input_index = 0;
  int cursor_position = 0;

  int cursor_col = 0;

  int current_key_pressed = 0;  // 0 means no key pressed

  ////////////////////

  struct sockaddr_in serv_addr;

  struct usb_keyboard_packet packet;
  int transferred;
  char keystate[12];

  if ((err = fbopen()) != 0) {
    fprintf(stderr, "Error: Could not open framebuffer: %d\n", err);
    exit(1);
  }
//////////Clear?
// for (rows = 0 ; rows < 24 ; rows++){
// for (col = 0 ; col < 64 ; col++) {

//   fbputchar(' ', rows, col);
//   //fbputchar('*', 23, col);
// }
// }

fbclear(0,0,0);

//fbputs("This is a long text that will automatically wrap.", 21, 50);
////////////////////
  /* Draw rows of asterisks across the top and bottom of the screen */
  for (col = 0 ; col < 64 ; col++) {
    fbputchar('*', 0, col);
    fbputchar('-', 20, col);   // Should split the screen
    fbputchar('*', 23, col);
  }

  //fbputs("Hello Adwyck Gupta 123", 4, 10);

  /* Open the keyboard */
  if ( (keyboard = openkeyboard(&endpoint_address)) == NULL ) {
    fprintf(stderr, "Did not find a keyboard\n");
    exit(1);
  }

  /* Create a TCP communications socket */
  if ( (sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0 ) {
    fprintf(stderr, "Error: Could not create socket\n");
    exit(1);
  }

  /* Get the server address */
  memset(&serv_addr, 0, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(SERVER_PORT);
  if ( inet_pton(AF_INET, SERVER_HOST, &serv_addr.sin_addr) <= 0) {
    fprintf(stderr, "Error: Could not convert host IP \"%s\"\n", SERVER_HOST);
    exit(1);
  }

  /* Connect the socket to the server */
  if ( connect(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
    fprintf(stderr, "Error: connect() failed.  Is the server running?\n");
    exit(1);
  }

  /* Start the network thread */
  pthread_create(&network_thread, NULL, network_thread_f, NULL);

  /* Look for and handle keypresses */
  input_buffer[0] = '\0';

  uint8_t prev_keycodes[2] = {0, 0};

  for (;;) {
    libusb_interrupt_transfer(keyboard, endpoint_address,
                              (unsigned char *) &packet, sizeof(packet),
                              &transferred, 0);

    if (transferred == sizeof(packet)) {

        // No keys pressed, update previous and skip
        if (packet.keycode[0] == 0 && packet.keycode[1] == 0) {
            prev_keycodes[0] = prev_keycodes[1] = 0;
            continue;
        }

        int key_handled = 0;
        for (int i = 0; i < 2; i++) {
            uint8_t current_keycode = packet.keycode[i];

            if (current_keycode == 0) continue;

            // Check if this key was already pressed previously
            int already_pressed = (current_keycode == prev_keycodes[0]) ||
                                  (current_keycode == prev_keycodes[1]);

            if (!already_pressed && !key_handled) {
                key = usbkey_to_ascii(current_keycode, packet.modifiers);

                if (key != 0) {  // Valid ASCII key
                    if (key == '\b') {
                        if (cursor_position > 0) {
                            for (int j = cursor_position - 1; j < input_index - 1; j++)
                                input_buffer[j] = input_buffer[j + 1];
                            input_index--;
                            cursor_position--;
                            input_buffer[input_index] = '\0';
                        }
                    } 
                    else if (key == '\n') {
                        write(sockfd, input_buffer, strlen(input_buffer));
                        memset(input_buffer, 0, BUFFER_SIZE);
                        input_index = cursor_position = 0;
                    }
                    else if (key == LEFT_KEY) {
                        if (cursor_position > 0) cursor_position--;
                    }
                    else if (key == RIGHT_KEY) {
                        if (cursor_position < input_index) cursor_position++;
                    }
                    else {
                        if (input_index < BUFFER_SIZE - 1) {
                            for (int j = input_index; j > cursor_position; j--)
                                input_buffer[j] = input_buffer[j - 1];
                            input_buffer[cursor_position++] = key;
                            input_index++;
                            input_buffer[input_index] = '\0';
                        }
                    }

                    key_handled = 1;  // Prevent handling multiple keys at once
                }
            }
        }

        // Update previous keys state at the end
        prev_keycodes[0] = packet.keycode[0];
        prev_keycodes[1] = packet.keycode[1];

        // Redraw screen each time
        for (col = 0; col < MAX_COLS; col++) {
            fbputchar(' ', INPUT_FIRST_ROW, col);
            fbputchar(' ', INPUT_SECOND_ROW, col);
        }

        if (input_index < MAX_COLS) {
            fbputs(input_buffer, INPUT_FIRST_ROW, 0);
            fbputchar(CURSOR_CHAR, INPUT_FIRST_ROW, cursor_position);
        } else {
            fbputs(input_buffer, INPUT_FIRST_ROW, 0);
            fbputs(input_buffer + MAX_COLS, INPUT_SECOND_ROW, 0);
            fbputchar(CURSOR_CHAR, INPUT_SECOND_ROW, cursor_position - MAX_COLS);
        }

        if (packet.keycode[0] == 0x29) { /* ESC pressed? */
            break;
        }
    }
}

  /* Terminate the network thread */
  pthread_cancel(network_thread);

  /* Wait for the network thread to finish */
  pthread_join(network_thread, NULL);

  return 0;
}

void *network_thread_f(void *ignored)
{
  char recvBuf[BUFFER_SIZE];
  int n;
  /////////////////////
  int rows, col;
int rws = 1;
  ////////////////////
  /* Receive data */
  while ( (n = read(sockfd, &recvBuf, BUFFER_SIZE - 1)) > 0 ) {
    recvBuf[n] = '\0';
    printf("%s", recvBuf);
    fbputs(recvBuf, rws, 0);
    rws++;
    ////////////////////////////// clear and scroll to top and clear the recieve screen
    if(rws > 19){
      for (rows = 1 ; rows < 20 ; rows++){
         for (col = 0 ; col < 64 ; col++) {
           fbputchar(' ', rows, col);
         }
         }
      rws = 1;

    }
    //////////////////////////
  }

  return NULL;
}

/////////////////////////////////////////////////////////////////// handling ASCII    CCVVVVVVVVVVVVVVVVVVVVVVVVVVVVVDDDDDDWWWWWWWWWWWWWWWrvfspace ships

int usbkey_to_ascii(uint8_t keycode, uint8_t modifiers)
{
    // Letters: keycodes 0x04 to 0x1d represent 'a' to 'z'
    if (keycode >= 0x04 && keycode <= 0x1d) {
        if (modifiers & (USB_LSHIFT | USB_RSHIFT))
            return 'A' + (keycode - 0x04);
        else
            return 'a' + (keycode - 0x04);
    }

    // Digits: keycodes 0x1e to 0x27 represent '1'-'9' and '0'
    if (keycode >= 0x1e && keycode <= 0x27) {
        if (modifiers & (USB_LSHIFT | USB_RSHIFT)) {
            // When shifted, map to symbols
            const char shifted_digits[] = {'!', '@', '#', '$', '%', '^', '&', '*', '(', ')'};
            return shifted_digits[keycode - 0x1e];
        } else {
            // Not shifted: map normally
            if (keycode == 0x27)
                return '0';
            else
                return '1' + (keycode - 0x1e);
        }
    }

    // Space: keycode 0x2c
    if (keycode == 0x2c)
        return ' ';

    // Punctuation and symbols:
    if (keycode == 0x2d) {  // '-' or '_'
        return (modifiers & (USB_LSHIFT | USB_RSHIFT)) ? '_' : '-';
    }
    if (keycode == 0x2e) {  // '=' or '+'
        return (modifiers & (USB_LSHIFT | USB_RSHIFT)) ? '+' : '=';
    }
    if (keycode == 0x2f) {  // '[' or '{'
        return (modifiers & (USB_LSHIFT | USB_RSHIFT)) ? '{' : '[';
    }
    if (keycode == 0x30) {  // ']' or '}'
        return (modifiers & (USB_LSHIFT | USB_RSHIFT)) ? '}' : ']';
    }
    if (keycode == 0x31) {  // '\' or '|'
        return (modifiers & (USB_LSHIFT | USB_RSHIFT)) ? '|' : '\\';
    }
    if (keycode == 0x33) {  // ';' or ':'
        return (modifiers & (USB_LSHIFT | USB_RSHIFT)) ? ':' : ';';
    }
    if (keycode == 0x34) {  // '\'' or '"'
        return (modifiers & (USB_LSHIFT | USB_RSHIFT)) ? '"' : '\'';
    }
    if (keycode == 0x35) {  // '`' or '~'
        return (modifiers & (USB_LSHIFT | USB_RSHIFT)) ? '~' : '`';
    }
    if (keycode == 0x36) {  // ',' or '<'
        return (modifiers & (USB_LSHIFT | USB_RSHIFT)) ? '<' : ',';
    }
    if (keycode == 0x37) {  // '.' or '>'
        return (modifiers & (USB_LSHIFT | USB_RSHIFT)) ? '>' : '.';
    }
    if (keycode == 0x38) {  // '/' or '?'
        return (modifiers & (USB_LSHIFT | USB_RSHIFT)) ? '?' : '/';
    }

    // Backspace: keycode 0x2a
    if (keycode == 0x2a){
      return '\b';
    }
    // Enter: keycode 0x28
    if (keycode == 0x28){
      return '\n';
    }
    // Left key
    if (keycode == 0x50){
      return LEFT_KEY;
    }
    //Right key
    if (keycode == 0x4f){
      return RIGHT_KEY;
    }
    // For keys not mapped here, return 0.
    return 0;
}

