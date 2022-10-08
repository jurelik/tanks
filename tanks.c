#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <string.h>
#include <math.h>
#include <enet/enet.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>

#define SCREEN_WIDTH 640
#define SCREEN_HEIGHT 480
#define PLAYER_SPEED 3
#define PLAYER_ROTATION_SPEED 3
#define BULLET_SIZE 4 //Must be an even number
#define BULLET_SPEED 1
#define BULLET_AMOUNT 16
#define PI 3.14159265358979323846

/* TYPES */
typedef struct {
  float posX;
  float posY;
  int angle;
  int bounces;
} Bullet;

typedef struct {
  Bullet bullets[BULLET_AMOUNT];
  int front;
  int back;
  int size;
} Bullet_queue;

typedef struct {
  uint8_t id;
  float posX;
  float posY;
  int angle;
  SDL_Texture *texture;
  Bullet_queue bullet_queue;
  int activeBullets;
  uint8_t up;
  uint8_t down;
  uint8_t left;
  uint8_t right;
  uint8_t buttonA;
  uint8_t buttonB;
  uint8_t buttonAIsDown;
  uint8_t buttonBIsDown;
} Player;

typedef struct {
  SDL_Renderer *renderer;
  SDL_Window *window;
  ENetAddress address;
  ENetHost *server;
  ENetHost *client;
  ENetEvent event;
  ENetPeer *peer;
  int enet_initialized;
  Player *localPlayer;
  Player players[15];
  uint8_t number_of_players;
  uint8_t isRunning;
  uint8_t up;
  uint8_t down;
  uint8_t left;
  uint8_t right;
  uint8_t buttonA;
  uint8_t buttonB;
  uint8_t buttonAIsDown;
  uint8_t buttonBIsDown;
} App;

/* ENUMS */
enum client_packet_type {
  CLIENT_POSITION_FLAG,
  CLIENT_STATE_FLAG
};

enum host_packet_type {
  HOST_POSITION_FLAG,
  HOST_STATE_FLAG
};

/* FUNCTION DEFINITIONS */
int createPlayer(Player *player, int posX, int posY);
void movePlayerForward(Player *player);
void movePlayerBackward(Player *player);
void shootBullet(Player *player);

App app = {0};

void cleanup() {
  if (app.window) { SDL_DestroyWindow(app.window); }
  if (app.renderer) { SDL_DestroyRenderer(app.renderer); }
  if (app.localPlayer->texture) { SDL_DestroyTexture(app.localPlayer->texture); }
  if (app.server) { enet_host_destroy(app.server); }
  if (app.client) { enet_host_destroy(app.client); }
  if (app.enet_initialized) { enet_deinitialize(); }

  SDL_Quit();
}

/* Enet logic */
int init_enet() {
  if (enet_initialize() != 0) {
    fprintf(stderr, "Failed to initialize Enet.\n");
    return EXIT_FAILURE;
  }

  app.enet_initialized = 1;
  return 0;
}

int init_server() {
  enet_address_set_host(&app.address, "127.0.0.1");
  app.address.port = 1234;

  app.server = enet_host_create(&app.address, 16, 1, 0, 0);
  if (app.server == NULL) {
    fprintf(stderr, "Failed to initialize an Enet server.\n");
    return EXIT_FAILURE;
  }

  printf("Enet server successfully initialized.\n");
  return 0;
}

int init_client() {
  enet_address_set_host(&app.address, "127.0.0.1");
  app.address.port = 1234;

  app.client = enet_host_create(NULL, 1, 1, 0, 0);
  if (app.client == NULL) {
    fprintf(stderr, "Failed to initialize an Enet client.\n");
    return EXIT_FAILURE;
  }

  printf("Enet client successfully initialized.\n");
  return 0;
}

/*
void client_send_position() {
  // Create memory block containing the local app state
  int sizeof_data = sizeof(uint8_t) + 2 * sizeof(uint16_t);
  uint8_t *data = malloc(sizeof_data);
  uint16_t posX = (uint16_t)app.localPlayer.posX;
  uint16_t posY = (uint16_t)app.localPlayer.posY;

  data[0] = CLIENT_POSITION_FLAG;
  memcpy(&data[1], &posX, sizeof(uint16_t));
  memcpy(&data[3], &posY, sizeof(uint16_t));

  ENetPacket *packet = enet_packet_create(data, sizeof_data, ENET_PACKET_FLAG_RELIABLE);
  enet_peer_send(app.peer, 0, packet);

  // Cleanup
  free(data);
}
*/

void host_send_position(ENetPeer *peer) {
  // Create memory block containing the player positions
  int sizeof_data = 2 * sizeof(uint8_t) + app.number_of_players * 2 * sizeof(uint16_t);
  uint8_t *data = malloc(sizeof_data);

  data[0] = HOST_POSITION_FLAG;
  data[1] = app.number_of_players;
  int position_index = 2;

  for (uint8_t i = 0; i < app.number_of_players; i++) {
    uint16_t posX = (uint16_t)app.players[i].posX;
    uint16_t posY = (uint16_t)app.players[i].posY;

    memcpy(&data[position_index], &posX, sizeof(uint16_t));
    position_index += 2;
    memcpy(&data[position_index], &posY, sizeof(uint16_t));
    position_index += 2;
  }

  ENetPacket *packet = enet_packet_create(data, sizeof_data, ENET_PACKET_FLAG_RELIABLE);
  enet_peer_send(peer, 0, packet);

  // Cleanup
  free(data);
}

int connect_to_host() {
  app.peer = enet_host_connect(app.client, &app.address, 1, 0);

  if (app.peer == NULL) {
    fprintf(stderr, "Peer not found.\n");
    return EXIT_FAILURE;
  }

  if (enet_host_service(app.client, &app.event, 5000) > 0 && app.event.type == ENET_EVENT_TYPE_CONNECT) {
    printf("Successfully connected to host.\n");

    return 0;
  }
  else {
    enet_peer_reset(app.peer);

    fprintf(stderr, "Failed to connect to host.\n");
    return EXIT_FAILURE;
  }
}

int host_or_join(char **argv) {
  if (!argv[1]) {
    fprintf(stderr, "Use the following format: %s < host | join >\n", argv[0]);
    return EXIT_FAILURE;
  }

  if (strcmp(argv[1], "host") == 0) { return init_server(); }
  else if (strcmp(argv[1], "join") == 0) {
    if (init_client() == EXIT_FAILURE) { return EXIT_FAILURE; }
    return connect_to_host();
  }
  else {
    fprintf(stderr, "Use the following format: %s < host | join >", argv[0]);
    return EXIT_FAILURE;
  }
}

void pollEnetServer() {
  while (enet_host_service(app.server, &app.event, 0) > 0) {
    switch (app.event.type) {
      case ENET_EVENT_TYPE_CONNECT:
        printf("New client connected from %x:%u.\n", app.event.peer->address.host, app.event.peer->address.port);

        //Assign ID to client (CHECK IF DATA GETS FREED AFTER PEER DISCONNECTS!! ASK ON IRC?)
        app.event.peer->data = malloc(sizeof(uint8_t));
        memcpy(app.event.peer->data, &app.number_of_players, sizeof(uint8_t));

        //Create player
        if (createPlayer(&app.players[app.number_of_players], 0, 0) == EXIT_FAILURE) { exit(EXIT_FAILURE); }

        //Send player position to client
        host_send_position(app.event.peer);

        break;
      case ENET_EVENT_TYPE_RECEIVE:
        // printf("A packet of length %lu containing %s was received on channel %u.\n", app.event.packet->dataLength, app.event.packet->data, app.event.channelID);

        uint8_t *data = (uint8_t *)app.event.packet->data;

        if (data[0] == CLIENT_POSITION_FLAG) {
          uint16_t posX;
          uint16_t posY;
          memcpy(&posX, &data[1], sizeof(uint16_t));
          memcpy(&posY, &data[3], sizeof(uint16_t));

          if (createPlayer(&app.players[0], posX, posY) == EXIT_FAILURE) { exit(EXIT_FAILURE); }
        }
        else if (data[0] == CLIENT_STATE_FLAG) {
          uint8_t *player_ID = (uint8_t *)app.event.peer->data;
          if (data[1]) { movePlayerForward(&app.players[*player_ID]); };
          if (data[2]) { movePlayerBackward(&app.players[*player_ID]); };
          if (data[3]) { app.players[*player_ID].angle -= PLAYER_ROTATION_SPEED; };
          if (data[4]) { app.players[*player_ID].angle += PLAYER_ROTATION_SPEED; };
          if (data[5] && !app.players[*player_ID].buttonAIsDown) { shootBullet(&app.players[*player_ID]); app.players[*player_ID].buttonAIsDown = 1; };
        }

        break;
      case ENET_EVENT_TYPE_DISCONNECT:
        printf("Client disconnected from %x:%u.\n", app.event.peer->address.host, app.event.peer->address.port);

        break;
      case ENET_EVENT_TYPE_NONE:
        break;
    }
  }
}

void pollEnetClient() {
  while (enet_host_service(app.client, &app.event, 0) > 0) {
    switch (app.event.type) {
      case ENET_EVENT_TYPE_CONNECT:
        printf("New client connected from %x:%u.\n", app.event.peer->address.host, app.event.peer->address.port);
        break;
      case ENET_EVENT_TYPE_RECEIVE:
        printf("A packet of length %lu containing %s was received on channel %u.\n", app.event.packet->dataLength, app.event.packet->data, app.event.channelID);
        uint8_t *data = (uint8_t *)app.event.packet->data;

        if (data[0] == HOST_POSITION_FLAG) {
          uint8_t number_of_players = data[1];
          printf("nop: %d\n", number_of_players);
          int position_index = 2;

          //Create players
          for (uint8_t i = 0; i < number_of_players; i++) {
            uint16_t posX;
            uint16_t posY;
            memcpy(&posX, &data[position_index], sizeof(uint16_t));
            position_index += 2;
            memcpy(&posY, &data[position_index], sizeof(uint16_t));
            position_index += 2;

            if (createPlayer(&app.players[app.number_of_players], posX, posY) == EXIT_FAILURE) { exit(EXIT_FAILURE); }
          }

          app.localPlayer = &app.players[app.number_of_players - 1]; //Last player in app.players is localPlayer
        }
        break;
      case ENET_EVENT_TYPE_DISCONNECT:
        printf("Client disconnected from %x:%u.\n", app.event.peer->address.host, app.event.peer->address.port);
        break;
      case ENET_EVENT_TYPE_NONE:
        break;
    }
  }
}

void pollEnet() {
  if (app.server) { pollEnetServer(); }
  else if (app.client) { pollEnetClient(); }
}

void sendEnetServer() {

}

void sendEnetClient() {
  // Create memory block containing the local app state
  int sizeof_data = 7 * sizeof(uint8_t);
  uint8_t *data = malloc(sizeof_data);

  data[0] = CLIENT_STATE_FLAG;
  data[1] = app.up;
  data[2] = app.down;
  data[3] = app.left;
  data[4] = app.right;
  data[5] = app.buttonA;
  data[6] = app.buttonB;

  ENetPacket *packet = enet_packet_create(data, sizeof_data, ENET_PACKET_FLAG_UNSEQUENCED);
  enet_peer_send(app.peer, 0, packet);

  // Cleanup
  free(data);
}

void sendEnet() {
  if (app.server) { sendEnetServer(); }
  else if (app.client) { sendEnetClient(); }
}

/* SDL Logic */
int initSDL() {
  //Init SDL
  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    fprintf(stderr, "Failed to initialize SDL: %s\n", SDL_GetError());
    return EXIT_FAILURE;
  }

  //Init SDL_image
  if (IMG_Init(IMG_INIT_PNG) == 0) {
    fprintf(stderr, "Failed to initialize SDL_image: %s\n", SDL_GetError());
    return EXIT_FAILURE;
  }

  //Create window
  app.window = SDL_CreateWindow("Tanks", 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 0);
  if (!app.window) {
    fprintf(stderr, "Failed to create a window: %s\n", SDL_GetError());
    return EXIT_FAILURE;
  }

  //Create renderer
  app.renderer = SDL_CreateRenderer(app.window, -1, SDL_RENDERER_ACCELERATED);
  if (!app.renderer) {
    fprintf(stderr, "Failed to initialize a renderer: %s\n", SDL_GetError());
    return EXIT_FAILURE;
  }

  return 0;
}

void handleKeyDown(SDL_KeyboardEvent *event) {
  if (event->repeat != 0) { return; };
  if (event->keysym.scancode == SDL_SCANCODE_UP) { app.up = 1; };
  if (event->keysym.scancode == SDL_SCANCODE_DOWN) { app.down = 1; };
  if (event->keysym.scancode == SDL_SCANCODE_LEFT) { app.left = 1; };
  if (event->keysym.scancode == SDL_SCANCODE_RIGHT) { app.right = 1; };
  if (event->keysym.scancode == SDL_SCANCODE_Z) { app.buttonA = 1; };
  if (event->keysym.scancode == SDL_SCANCODE_X) { app.buttonB = 1; };
}

void handleKeyUp(SDL_KeyboardEvent *event) {
  if (event->keysym.scancode == SDL_SCANCODE_UP) { app.up = 0; };
  if (event->keysym.scancode == SDL_SCANCODE_DOWN) { app.down = 0; };
  if (event->keysym.scancode == SDL_SCANCODE_LEFT) { app.left = 0; };
  if (event->keysym.scancode == SDL_SCANCODE_RIGHT) { app.right = 0; };
  if (event->keysym.scancode == SDL_SCANCODE_Z) { app.buttonA = 0; app.buttonAIsDown = 0; };
  if (event->keysym.scancode == SDL_SCANCODE_X) { app.buttonB = 0; };
}

void pollEvents() {
  SDL_Event event;

  while (SDL_PollEvent(&event)) {
    switch (event.type) {
      case SDL_QUIT:
        app.isRunning = 0;
        break;
      case SDL_KEYDOWN:
        handleKeyDown(&event.key);
        break;
      case SDL_KEYUP:
        handleKeyUp(&event.key);
        break;
    }
  }
}

//Create and return a SDL_Texture
SDL_Texture *loadTexture(char *filename) {
  SDL_Texture *texture;
  texture = IMG_LoadTexture(app.renderer, filename);

  return texture;
}

//Copy texture to a rectangle
void blit(SDL_Texture *texture, int x, int y, int angle) {
  SDL_Rect dest = { x, y };
  SDL_QueryTexture(texture, NULL, NULL, &dest.w, &dest.h); //Get texture width/height and apply to dest
  SDL_RenderCopyEx(app.renderer, texture, NULL, &dest, angle, NULL, SDL_FLIP_NONE);
}

/* Player logic */
int createPlayer(Player *player, int posX, int posY) {
  srand(time(NULL)); //Seed the random generator
  if (!posX) posX = rand() % 631 + 10;
  if (!posY) posY = rand() % 471 + 10;

  //Create player
  memset(player, 0, sizeof(Player));
  player->id = app.number_of_players;
  player->posX = posX;
  player->posY = posY;
  player->angle = 0;
  player->bullet_queue.size = 0;

  //Load texture for player
  player->texture = loadTexture("tank.png");
  if (!player->texture) {
    fprintf(stderr, "Failed to load player texture: %s\n", SDL_GetError());
    return EXIT_FAILURE;
  }

  app.number_of_players++; //Increase number of players

  return 0;
}

void drawPlayer(Player *player) {
  //Round positions to int
  int posX = (int)floor(player->posX);
  int posY = (int)floor(player->posY);

  blit(player->texture, posX, posY, player->angle);
}

void movePlayerForward(Player *player) {
  player->posY -= cos(player->angle * PI/180) * PLAYER_SPEED;
  player->posX += sin(player->angle * PI/180) * PLAYER_SPEED;
}

void movePlayerBackward(Player *player) {
  player->posY += cos(player->angle * PI/180) * PLAYER_SPEED;
  player->posX -= sin(player->angle * PI/180) * PLAYER_SPEED;
}

/* Bullet logic */
int bullet_queue_is_full(Bullet_queue *bullet_queue) {
  printf("%d\n", bullet_queue->size == BULLET_AMOUNT);
  return bullet_queue->size == BULLET_AMOUNT;
}

int bullet_queue_is_empty(Bullet_queue *bullet_queue) {
  return bullet_queue->size == 0;
}

void bullet_dequeue(Bullet_queue *bullet_queue, Bullet *bullet) {
  if (bullet_queue_is_empty(bullet_queue)) { return; }

  bullet_queue->front = (bullet_queue->front + 1) % BULLET_AMOUNT;
  bullet_queue->size--;
}

void bullet_enqueue(Bullet_queue *bullet_queue, Bullet *bullet) {
  if (bullet_queue_is_full(bullet_queue)) { bullet_dequeue(bullet_queue, bullet); }

  bullet_queue->bullets[bullet_queue->back] = *bullet;
  bullet_queue->back = (bullet_queue->back + 1) % BULLET_AMOUNT;

  if (bullet_queue->size < BULLET_AMOUNT) { bullet_queue->size++; }
}

void shootBullet(Player *player) {
  //Get player width & height
  int playerW, playerH;
  SDL_QueryTexture(player->texture, NULL, NULL, &playerW, &playerH);

  //Create bullet
  Bullet bullet = {0};
  bullet.posX = player->posX + playerW / 2 - (BULLET_SIZE / 2 - 1);
  bullet.posY = player->posY + playerH / 2- (BULLET_SIZE / 2 - 1);
  bullet.angle = player->angle;

  bullet_enqueue(&player->bullet_queue, &bullet);
}

void updateBulletPositions(Player *player) {
  for (int i = 0; i < player->bullet_queue.size; i++) {
    int index = (player->bullet_queue.front + i) % BULLET_AMOUNT;

    player->bullet_queue.bullets[index].posY -= cos(player->bullet_queue.bullets[index].angle * PI/180) * BULLET_SPEED;
    player->bullet_queue.bullets[index].posX += sin(player->bullet_queue.bullets[index].angle * PI/180) * BULLET_SPEED;
  }
}

void drawBullets(Player *player) {
  for (int i = 0; i < player->bullet_queue.size; i++) {
    int index = (player->bullet_queue.front + i) % BULLET_AMOUNT;

    //Round positions to int
    int posX = (int)floor(player->bullet_queue.bullets[index].posX);
    int posY = (int)floor(player->bullet_queue.bullets[index].posY);

    //Draw rectangle
    SDL_Rect rect = {posX, posY, BULLET_SIZE, BULLET_SIZE};
    SDL_SetRenderDrawColor(app.renderer, 220, 0, 0, 255);
    SDL_RenderDrawRect(app.renderer, &rect);
  }
}

/* Game loop logic */
int load() {
  if (app.server) {
    if (createPlayer(&app.players[app.number_of_players], 0, 0) == EXIT_FAILURE) { return EXIT_FAILURE; }

    app.localPlayer = &app.players[app.number_of_players - 1]; //Create a pointer to the local player
  }

  return 0;
}

void update() {
  if (!app.number_of_players) { return; } //Skip if no players

  if (app.up) { movePlayerForward(app.localPlayer); };
  if (app.down) { movePlayerBackward(app.localPlayer); };
  if (app.left) { app.localPlayer->angle -= PLAYER_ROTATION_SPEED; };
  if (app.right) { app.localPlayer->angle += PLAYER_ROTATION_SPEED; };
  if (app.buttonA && !app.buttonAIsDown) { shootBullet(app.localPlayer); app.buttonAIsDown = 1; };

  updateBulletPositions(app.localPlayer);
}

void draw() {
  if (!app.number_of_players) { return; } //Skip if no players

  //Draw background
  SDL_SetRenderDrawColor(app.renderer, 25, 25, 25, 255);
  SDL_RenderClear(app.renderer);

  for (int i = 0; i < app.number_of_players; i++) {
    //printf("Player %d: posX = %f\n", i, app.players[i].posX);
    //printf("Player %d: posY = %f\n", i, app.players[i].posY);
    drawPlayer(&app.players[i]); //Draw player
    drawBullets(&app.players[i]); //Draw bullets
  }

  //Present
  SDL_RenderPresent(app.renderer);
  SDL_Delay(16);
}

int main(int argc, char **argv) {
  app.isRunning = 1;
  atexit(cleanup); //Assign a cleanup function
  if (initSDL() == EXIT_FAILURE) { return -1; } //Initialize SDL
  if (init_enet() == EXIT_FAILURE) { return -1; } //Initialize ENet

  if (host_or_join(argv) == EXIT_FAILURE) { return -1; } //Host or join
  if (load() == EXIT_FAILURE) { return -1; }; //Load state


  while (app.isRunning) {
    pollEnet();
    pollEvents();
    update();
    draw();
    sendEnet();
  }

  return 0;
}
