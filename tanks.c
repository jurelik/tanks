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
#define TILE_SIZE 16
#define MAP_WIDTH 40
#define MAP_HEIGHT 40
#define PLAYER_SIZE 16
#define PLAYER_SPEED 3
#define PLAYER_ROTATION_SPEED 3
#define BULLET_SIZE 4 //Must be an even number
#define BULLET_SPEED 1
#define BULLET_AMOUNT 16
#define BULLET_TIMEOUT 1 //In seconds
#define PI 3.14159265358979323846

/* TYPES */
typedef struct {
  float pos_x;
  float pos_y;
  struct timeval time_created;
  int16_t angle;
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
  float pos_x;
  float pos_y;
  int16_t angle;
  SDL_Texture *texture;
  Bullet_queue bullet_queue;
  uint8_t active_bullets;
  uint8_t up;
  uint8_t down;
  uint8_t left;
  uint8_t right;
  uint8_t button_a;
  uint8_t button_b;
  uint8_t button_a_is_down;
  uint8_t button_b_is_down;
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
  uint8_t map[MAP_HEIGHT][MAP_WIDTH];
  Player *local_player;
  Player players[15];
  uint8_t num_of_players;
  uint8_t current_id;
  uint8_t is_running;
  uint8_t up;
  uint8_t down;
  uint8_t left;
  uint8_t right;
  uint8_t button_a;
  uint8_t button_b;
  uint8_t button_a_is_down;
  uint8_t button_b_is_down;
} App;

/* ENUMS */
enum client_packet_type {
  CLIENT_STATE_PACKET
};

enum host_packet_type {
  HOST_POSITION_PACKET,
  HOST_MAP_PACKET,
  HOST_STATE_PACKET,
  HOST_PLAYER_JOINED_PACKET,
  HOST_PLAYER_LEFT_PACKET,
  HOST_PLAYER_HIT_PACKET,
  HOST_NEW_BULLET_PACKET
};

/* FUNCTION DEFINITIONS */
int create_player(Player *, uint8_t, uint16_t, uint16_t);
int delete_player(uint8_t *);
void movePlayerForward(Player *);
void movePlayerBackward(Player *);
void shoot_bullet(Player *, uint16_t, uint16_t, int16_t);
Player *get_player_by_id(uint8_t);

App app = {0};

void cleanup() {
  if (app.window) SDL_DestroyWindow(app.window);
  if (app.renderer) SDL_DestroyRenderer(app.renderer);
  if (app.local_player->texture) SDL_DestroyTexture(app.local_player->texture);
  if (app.server) enet_host_destroy(app.server);
  if (app.client) enet_host_destroy(app.client);
  if (app.enet_initialized) enet_deinitialize();

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

void host_send_position(ENetPeer *peer) {
  /* PACKET STRUCTURE */
  /*                |------------*number of players------------|
  --------------------------------------------------------------
  |  flag  | num_p  |  p_id  |     pos_x      |     pos_y      |
  --------------------------------------------------------------
  */

  // Create memory block containing the player positions
  int sizeof_data = 2 * sizeof(uint8_t);
  sizeof_data += app.num_of_players * (sizeof(uint8_t) + 2 * sizeof(uint16_t));
  uint8_t *data = malloc(sizeof_data);
  ENetPacket *packet;

  data[0] = HOST_POSITION_PACKET;
  data[1] = app.num_of_players;
  int position_index = 2;

  for (uint8_t i = 0; i < app.num_of_players; i++) {
    uint16_t id = (uint8_t)app.players[i].id;
    uint16_t pos_x = (uint16_t)app.players[i].pos_x;
    uint16_t pos_y = (uint16_t)app.players[i].pos_y;

    memcpy(&data[position_index], &id, sizeof(uint8_t));
    position_index += 1;
    memcpy(&data[position_index], &pos_x, sizeof(uint16_t));
    position_index += 2;
    memcpy(&data[position_index], &pos_y, sizeof(uint16_t));
    position_index += 2;
  }

  packet = enet_packet_create(data, sizeof_data, ENET_PACKET_FLAG_RELIABLE);
  enet_peer_send(peer, 0, packet);

  // Cleanup
  free(data);
}

void host_send_map(ENetPeer *peer) {
  /* PACKET STRUCTURE */
  /*
  -----------------------------------------------------
  |  flag  |               map_array                  |
  -----------------------------------------------------
  */

  // Create memory block containing the map array
  int sizeof_data = sizeof(uint8_t) + sizeof(app.map);
  uint8_t *data = malloc(sizeof_data);
  ENetPacket *packet;

  data[0] = HOST_MAP_PACKET;
  int position_index = 1;

  memcpy(&data[position_index], &app.map, sizeof(app.map));

  packet = enet_packet_create(data, sizeof_data, ENET_PACKET_FLAG_RELIABLE);
  enet_peer_send(peer, 0, packet);

  // Cleanup
  free(data);
}

void host_send_player_joined() {
  /* PACKET STRUCTURE
  --------------------------------------------
  |  flag  |     pos_x      |     pos_y      |
  --------------------------------------------
  */

  // Create memory block containing the player positions
  int sizeof_data = 2 * sizeof(uint8_t) + 2 * sizeof(uint16_t);
  uint8_t *data = malloc(sizeof_data);
  ENetPacket *packet;

  data[0] = HOST_PLAYER_JOINED_PACKET;
  data[1] = app.players[app.num_of_players - 1].id;
  int position_index = 2;

  uint16_t pos_x = (uint16_t)app.players[app.num_of_players - 1].pos_x;
  uint16_t pos_y = (uint16_t)app.players[app.num_of_players - 1].pos_y;

  memcpy(&data[position_index], &pos_x, sizeof(uint16_t));
  position_index += 2;
  memcpy(&data[position_index], &pos_y, sizeof(uint16_t));

  packet = enet_packet_create(data, sizeof_data, ENET_PACKET_FLAG_RELIABLE);
  enet_host_broadcast(app.server, 0, packet);

  // Cleanup
  free(data);
}

void host_send_player_left(uint8_t *id) {
  /* PACKET STRUCTURE
  -------------------
  |  flag  |  p_id  |
  -------------------
  */

  // Create memory block containing the player id
  int sizeof_data = 2 * sizeof(uint8_t);
  uint8_t *data = malloc(sizeof_data);
  ENetPacket *packet;

  data[0] = HOST_PLAYER_LEFT_PACKET;
  data[1] = *id;

  packet = enet_packet_create(data, sizeof_data, ENET_PACKET_FLAG_RELIABLE);
  enet_host_broadcast(app.server, 0, packet);

  // Cleanup
  free(data);
}

int connect_to_host() {
  app.peer = enet_host_connect(app.client, &app.address, 1, 0);

  if (app.peer == NULL) {
    fprintf(stderr, "Peer not found.\n");
    return EXIT_FAILURE;
  }

  int res = enet_host_service(app.client, &app.event, 5000);
  if (res > 0 && app.event.type == ENET_EVENT_TYPE_CONNECT) {
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

void handle_host_event_connect() {
  printf("New client connected from %x:%u.\n",
    app.event.peer->address.host, app.event.peer->address.port);

  //Assign ID to client CHECK IF DATA GETS FREED AFTER PEER DISCONNECTS!!
  //ASK ON IRC?
  app.event.peer->data = malloc(sizeof(uint8_t));
  memcpy(app.event.peer->data, &app.current_id, sizeof(uint8_t));

  //Create player
  Player *player = &app.players[app.num_of_players];
  uint8_t res = create_player(player, app.current_id, 0, 0);
  if (res == EXIT_FAILURE) { exit(EXIT_FAILURE); }

  host_send_position(app.event.peer); //Send player position to client
  host_send_map(app.event.peer); //Send map to client
  host_send_player_joined(); //Broadcast player joined to all
}

void handle_host_event_receive() {
  uint8_t *data = (uint8_t *)app.event.packet->data;

  if (data[0] == CLIENT_STATE_PACKET) {
    uint8_t *player_ID = (uint8_t *)app.event.peer->data;
    Player *player = get_player_by_id(*player_ID);

    if (data[1]) { movePlayerForward(player); };
    if (data[2]) { movePlayerBackward(player); };
    if (data[3]) { player->angle -= PLAYER_ROTATION_SPEED; };
    if (data[4]) { player->angle += PLAYER_ROTATION_SPEED; };
    if (data[5] && !player->button_a_is_down) {
      shoot_bullet(player, 0, 0, 0);
      player->button_a_is_down = 1;
    };
    if (!data[5] && player->button_a_is_down) { player->button_a_is_down = 0; };
  }
}

void handle_host_event_disconnect() {
  printf("Client disconnected from %x:%u.\n",
    app.event.peer->address.host, app.event.peer->address.port);

  host_send_player_left(app.event.peer->data);
  uint8_t res = delete_player(app.event.peer->data);
  if ( res == EXIT_FAILURE) { exit(EXIT_FAILURE); }
}

void poll_enet_host() {
  while (enet_host_service(app.server, &app.event, 0) > 0) {
    switch (app.event.type) {
      case ENET_EVENT_TYPE_CONNECT:
        handle_host_event_connect();
        break;
      case ENET_EVENT_TYPE_RECEIVE:
        handle_host_event_receive();
        break;
      case ENET_EVENT_TYPE_DISCONNECT:
        handle_host_event_disconnect();
        break;
      case ENET_EVENT_TYPE_NONE:
        break;
    }
  }
}

void handle_client_packet_position(uint8_t *data) {
  uint8_t num_of_players = data[1];
  int data_index = 2;

  //Create players
  for (uint8_t i = 0; i < num_of_players; i++) {
    uint8_t id;
    uint16_t pos_x;
    uint16_t pos_y;

    memcpy(&id, &data[data_index], sizeof(uint8_t));
    data_index += 1;
    memcpy(&pos_x, &data[data_index], sizeof(uint16_t));
    data_index += 2;
    memcpy(&pos_y, &data[data_index], sizeof(uint16_t));
    data_index += 2;

    uint8_t res = create_player(&app.players[i], id, pos_x, pos_y);
    if (res == EXIT_FAILURE) { exit(EXIT_FAILURE); }
  }

  //Last player in app.players is local_player
  app.local_player = &app.players[app.num_of_players - 1];
  printf("Your id is: %d\n", app.local_player->id);
}

void handle_client_packet_map(uint8_t *data) {
    int data_index = 1;

    //Copy map data
    memcpy(&app.map, &data[data_index], MAP_HEIGHT * MAP_WIDTH);
}

void handle_client_packet_state(uint8_t *data) {
  int data_index = 1;

  for (uint8_t i = 0; i < app.num_of_players; i++) {
    //Update player positions
    uint16_t pos_x;
    uint16_t pos_y;
    int16_t angle;
    memcpy(&pos_x, &data[data_index], sizeof(uint16_t));
    data_index += 2;
    memcpy(&pos_y, &data[data_index], sizeof(uint16_t));
    data_index += 2;
    memcpy(&angle, &data[data_index], sizeof(int16_t));
    data_index += 2;

    app.players[i].pos_x = pos_x;
    app.players[i].pos_y = pos_y;
    app.players[i].angle = angle;
  }
}

void handle_client_packet_player_joined(uint8_t *data) {
  uint8_t id = data[1];
  uint8_t data_index = 2;
  uint16_t pos_x;
  uint16_t pos_y;

  //When a new player joins they receive a HOST_POSITION_PACKET
  //packet privately & HOST_PLAYER_JOINED_PACKET packet via broadcast
  //so they need to ignore the HOST_PLAYER_JOINED_PACKET
  if (get_player_by_id(id)) return;

  memcpy(&pos_x, &data[data_index], sizeof(uint16_t));
  data_index += 2;
  memcpy(&pos_y, &data[data_index], sizeof(uint16_t));

  Player *player = &app.players[app.num_of_players];
  uint8_t res = create_player(player, id, pos_x, pos_y);
  if (res == EXIT_FAILURE) { exit(EXIT_FAILURE); }
}

void handle_client_packet_player_left(uint8_t *data) {
  uint8_t id = data[1];
  if (delete_player(&id) == EXIT_FAILURE) { exit(EXIT_FAILURE); }
}

void handle_client_packet_player_hit(uint8_t *data) {
  uint8_t id_hit = data[1];
  uint8_t id_shooter = data[2];

  printf("Player %d was shot by player %d\n", id_hit, id_shooter);
}

void handle_client_packet_new_bullet(uint8_t *data) {
  uint8_t id = data[1];
  uint8_t data_index = 2;
  uint16_t pos_x;
  uint16_t pos_y;
  int16_t angle;

  if (id == app.local_player->id) return; //Ignore if own bullet
  Player *player = get_player_by_id(id);

  memcpy(&pos_x, &data[data_index], sizeof(uint16_t));
  data_index += 2;
  memcpy(&pos_y, &data[data_index], sizeof(uint16_t));
  data_index += 2;
  memcpy(&angle, &data[data_index], sizeof(int16_t));

  shoot_bullet(player, pos_x, pos_y, angle);
}

void handle_client_event_receive() {
  uint8_t *data = (uint8_t *)app.event.packet->data;

  if (data[0] == HOST_POSITION_PACKET) handle_client_packet_position(data);
  else if (data[0] == HOST_MAP_PACKET) handle_client_packet_map(data);
  else if (data[0] == HOST_STATE_PACKET) handle_client_packet_state(data);
  else if (data[0] == HOST_PLAYER_JOINED_PACKET) handle_client_packet_player_joined(data);
  else if (data[0] == HOST_PLAYER_LEFT_PACKET) handle_client_packet_player_left(data);
  else if (data[0] == HOST_PLAYER_HIT_PACKET) handle_client_packet_player_hit(data);
  else if (data[0] == HOST_NEW_BULLET_PACKET) handle_client_packet_new_bullet(data);
}

void poll_enet_client() {
  while (enet_host_service(app.client, &app.event, 0) > 0) {
    switch (app.event.type) {
      case ENET_EVENT_TYPE_CONNECT:
        break;
      case ENET_EVENT_TYPE_RECEIVE:
        handle_client_event_receive();
        break;
      case ENET_EVENT_TYPE_DISCONNECT:
        printf("Player %d disconnected.\n", *(uint8_t *)app.event.peer->data);
        break;
      case ENET_EVENT_TYPE_NONE:
        break;
    }
  }
}

void poll_enet() {
  if (app.server) { poll_enet_host(); }
  else if (app.client) { poll_enet_client(); }
}

void send_enet_host_state() {
  /* PACKET STRUCTURE */
  /*       |----------------*number of players----------------|
  -------------------------------------------------------------
  |  flag  |     pos_x      |     pos_y      |     angle      |
  -------------------------------------------------------------
  */

  // Create memory block containing all player positions
  int sizeof_data = sizeof(uint8_t) + 3 * app.num_of_players * sizeof(uint16_t);
  uint8_t *data = malloc(sizeof_data);
  uint8_t data_index = 1;
  ENetPacket *packet;

  data[0] = HOST_STATE_PACKET;

  for (uint8_t i = 0; i < app.num_of_players; i++) {
    uint16_t pos_x = app.players[i].pos_x;
    uint16_t pos_y = app.players[i].pos_y;
    int16_t angle = app.players[i].angle;

    memcpy(&data[data_index], &pos_x, sizeof(uint16_t));
    data_index += 2;
    memcpy(&data[data_index], &pos_y, sizeof(uint16_t));
    data_index += 2;
    memcpy(&data[data_index], &angle, sizeof(int16_t));
    data_index += 2;
  }

  packet = enet_packet_create(data, sizeof_data, ENET_PACKET_FLAG_UNSEQUENCED);
  enet_host_broadcast(app.server, 0, packet);

  //Cleanup
  free(data);
}

void send_enet_host_new_bullet(Player *player, Bullet *bullet) {
  /* PACKET STRUCTURE
  ----------------------------------------------------------------------
  |  flag  |  p_id  |     pos_x      |     pos_y      |     angle      |
  ----------------------------------------------------------------------
  */

  // Create memory block containing all bullet information
  int sizeof_data = 2 * sizeof(uint8_t) + 3 * sizeof(uint16_t);
  uint8_t *data = malloc(sizeof_data);
  uint8_t data_index = 1;
  ENetPacket *packet;

  data[0] = HOST_NEW_BULLET_PACKET;
  memcpy(&data[data_index], &player->id, sizeof(uint8_t));
  data_index += 1;

  uint16_t pos_x = bullet->pos_x;
  uint16_t pos_y = bullet->pos_y;
  int16_t angle = bullet->angle;

  memcpy(&data[data_index], &pos_x, sizeof(uint16_t));
  data_index += 2;
  memcpy(&data[data_index], &pos_y, sizeof(uint16_t));
  data_index += 2;
  memcpy(&data[data_index], &angle, sizeof(int16_t));

  packet = enet_packet_create(data, sizeof_data, ENET_PACKET_FLAG_UNSEQUENCED);
  enet_host_broadcast(app.server, 0, packet);

  //Cleanup
  free(data);
}

void send_enet_host_player_hit(Player *p_hit, Player *p_shooter) {
  /* PACKET STRUCTURE
  ----------------------------
  |  flag  |p_hit_id|p_sht_id|
  ----------------------------
  */

  // Create memory block containing id of player that was hit and the shooter
  int sizeof_data = 3 * sizeof(uint8_t);
  uint8_t *data = malloc(sizeof_data);
  ENetPacket *packet;

  data[0] = HOST_PLAYER_HIT_PACKET;
  data[1] = p_hit->id;
  data[2] = p_shooter->id;

  packet = enet_packet_create(data, sizeof_data, ENET_PACKET_FLAG_UNSEQUENCED);
  enet_host_broadcast(app.server, 0, packet);

  //Cleanup
  free(data);
}

void send_enet_client_state() {
  /* PACKET STRUCTURE
  ----------------------------------------------------------------
  |  flag  |   up   |  down  |  left  | right  | but_a  | but_b  |
  ----------------------------------------------------------------
  */

  // Create memory block containing the local app state
  int sizeof_data = 7 * sizeof(uint8_t);
  uint8_t *data = malloc(sizeof_data);
  ENetPacket *packet;

  data[0] = CLIENT_STATE_PACKET;
  data[1] = app.up;
  data[2] = app.down;
  data[3] = app.left;
  data[4] = app.right;
  data[5] = app.button_a;
  data[6] = app.button_b;

  packet = enet_packet_create(data, sizeof_data, ENET_PACKET_FLAG_UNSEQUENCED);
  enet_peer_send(app.peer, 0, packet);

  // Cleanup
  free(data);
}

void send_enet() {
  if (app.server) { send_enet_host_state(); }
  else if (app.client) { send_enet_client_state(); }
}

/* SDL Logic */
uint8_t init_SDL() {
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
  int scancode = event->keysym.scancode;

  if (event->repeat != 0) { return; };
  if (scancode == SDL_SCANCODE_UP) app.up = 1;
  if (scancode == SDL_SCANCODE_DOWN) app.down = 1;
  if (scancode == SDL_SCANCODE_LEFT) app.left = 1;
  if (scancode == SDL_SCANCODE_RIGHT) app.right = 1;
  if (scancode == SDL_SCANCODE_Z) app.button_a = 1;
  if (scancode == SDL_SCANCODE_X) app.button_b = 1;
}

void handleKeyUp(SDL_KeyboardEvent *event) {
  int scancode = event->keysym.scancode;

  if (scancode == SDL_SCANCODE_UP) app.up = 0;
  if (scancode == SDL_SCANCODE_DOWN) app.down = 0;
  if (scancode == SDL_SCANCODE_LEFT) app.left = 0;
  if (scancode == SDL_SCANCODE_RIGHT) app.right = 0;
  if (scancode == SDL_SCANCODE_Z) {
    app.button_a = 0;
    app.button_a_is_down = 0;
  }
  if (scancode == SDL_SCANCODE_X) app.button_b = 0;
}

void poll_events() {
  SDL_Event event;

  while (SDL_PollEvent(&event)) {
    switch (event.type) {
      case SDL_QUIT:
        app.is_running = 0;
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
void blit(SDL_Texture *texture, int x, int y, int16_t angle) {
  SDL_Rect dest = { x, y };

  //Get texture width/height and apply to dest
  SDL_QueryTexture(texture, NULL, NULL, &dest.w, &dest.h);
  SDL_RenderCopyEx(app.renderer, texture, NULL, &dest, angle, NULL, SDL_FLIP_NONE);
}

/* Map logic */
void generate_map() {
 app.map[5][5] = 1;
 app.map[5][6] = 1;
 app.map[5][7] = 1;
 app.map[8][5] = 1;
 app.map[8][6] = 1;
 app.map[8][7] = 1;
 app.map[9][5] = 1;
 app.map[10][5] = 1;
 app.map[11][5] = 1;
 app.map[12][5] = 1;
}

void draw_map() {
  for (int i = 0; i < MAP_HEIGHT; i++) {
    for (int j = 0; j < MAP_WIDTH; j++) {
      if (app.map[i][j] == 0) { continue; }

      uint16_t pos_x = j * TILE_SIZE;
      uint16_t pos_y = i * TILE_SIZE;

      //Draw rectangle
      SDL_Rect rect = {pos_x, pos_y, TILE_SIZE, TILE_SIZE};
      SDL_SetRenderDrawColor(app.renderer, 0, 0, 255, 255);
      SDL_RenderDrawRect(app.renderer, &rect);
    }
  }
}

/* Player logic */
int create_player(Player *player, uint8_t id, uint16_t pos_x, uint16_t pos_y) {
  srand(time(NULL)); //Seed the random generator
  if (!pos_x) pos_x = rand() % 631 + 10;
  if (!pos_y) pos_y = rand() % 471 + 10;

  //Create player
  memset(player, 0, sizeof(Player));
  player->id = id;
  player->pos_x = pos_x;
  player->pos_y = pos_y;
  player->angle = 0;
  player->bullet_queue.size = 0;

  //Load texture for player
  player->texture = loadTexture("tank.png");
  if (!player->texture) {
    fprintf(stderr, "Failed to load player texture: %s\n", SDL_GetError());
    return EXIT_FAILURE;
  }

  app.num_of_players++; //Increase number of players
  app.current_id++; //Increase current id

  return 0;
}

int delete_player(uint8_t *id) {
  uint8_t local_player_id = app.local_player->id;

  for (uint8_t i = 0; i < app.num_of_players; i++) {
    if (app.players[i].id == *id) {
      for (uint8_t j = i; j < app.num_of_players - 1; j++) {
        app.players[j] = app.players[j + 1];
      }

      app.num_of_players--; //Decrement number of players

      //Update local player pointer
      Player *local_player = get_player_by_id(local_player_id);
      app.local_player = local_player;
      return 0;
    }
  }

  return EXIT_FAILURE;
}

Player *get_player_by_id(uint8_t id) {
  Player *player;

  for (uint8_t i = 0; i < app.num_of_players; i++) {
    if (app.players[i].id == id) {
      player = &app.players[i];
      return player;
    }
  }

  return NULL;
}

uint8_t player_collided(Player *p, uint16_t *pos_x_tank, uint16_t *pos_y_tank) {
  //Check map collisions
  for (int i = 0; i < MAP_HEIGHT; i++) {
    for (int j = 0; j < MAP_WIDTH; j++) {
      if (app.map[i][j] == 0) { continue; }

      //Create wall rectangle
      uint16_t pos_x_wall = j * TILE_SIZE;
      uint16_t pos_y_wall = i * TILE_SIZE;
      SDL_Rect rect_wall = {pos_x_wall, pos_y_wall, TILE_SIZE, TILE_SIZE};

      //Create tank rectangle
      SDL_Rect rect_tank = {*pos_x_tank, *pos_y_tank, PLAYER_SIZE, PLAYER_SIZE};

      if (SDL_HasIntersection(&rect_wall, &rect_tank) == SDL_TRUE) { return 1; }
    }
  }

  //Check other player collisions
  for (int i = 0; i < app.num_of_players; i++) {
    if (p->id == app.players[i].id) { continue; } //Ignore self

    //Create other player rectangle
    uint16_t pos_x_other = app.players[i].pos_x;
    uint16_t pos_y_other = app.players[i].pos_y;
    SDL_Rect rect_other = {pos_x_other, pos_y_other, PLAYER_SIZE, PLAYER_SIZE};

    //Create tank rectangle
    SDL_Rect rect_tank = {*pos_x_tank, *pos_y_tank, PLAYER_SIZE, PLAYER_SIZE};

    if (SDL_HasIntersection(&rect_other, &rect_tank) == SDL_TRUE) { return 1; }
  }

  return 0;
}

void drawPlayer(Player *p) {
  //Round positions to int
  int pos_x = (int)floor(p->pos_x);
  int pos_y = (int)floor(p->pos_y);

  blit(p->texture, pos_x, pos_y, p->angle);
}

void movePlayerForward(Player *p) {
  float new_pos_xf = p->pos_x + sin(p->angle * PI/180) * PLAYER_SPEED;
  float new_pos_yf = p->pos_y - cos(p->angle * PI/180) * PLAYER_SPEED;
  uint16_t new_pos_xi = (uint16_t)new_pos_xf;
  uint16_t new_pos_yi = (uint16_t)new_pos_yf;

  if(player_collided(p, &new_pos_xi, &new_pos_yi)) { return; }

  //Move player
  p->pos_x = new_pos_xf;
  p->pos_y = new_pos_yf;
}

void movePlayerBackward(Player *p) {
  float new_pos_xf = p->pos_x - sin(p->angle * PI/180) * PLAYER_SPEED;
  float new_pos_yf = p->pos_y + cos(p->angle * PI/180) * PLAYER_SPEED;
  uint16_t new_pos_xi = (uint16_t)new_pos_xf;
  uint16_t new_pos_yi = (uint16_t)new_pos_yf;

  if(player_collided(p, &new_pos_xi, &new_pos_yi)) { return; }

  //Move player
  p->pos_x = new_pos_xf;
  p->pos_y = new_pos_yf;
}

/* Bullet logic */
int bullet_queue_is_full(Bullet_queue *bullet_queue) {
  return bullet_queue->size == BULLET_AMOUNT;
}

int bullet_queue_is_empty(Bullet_queue *bullet_queue) {
  return bullet_queue->size == 0;
}

void bullet_dequeue(Bullet_queue *bullet_queue, Bullet *bullet) {
  if (bullet_queue_is_empty(bullet_queue)) return;

  bullet_queue->front = (bullet_queue->front + 1) % BULLET_AMOUNT;
  bullet_queue->size--;
}

void bullet_enqueue(Bullet_queue *bullet_queue, Bullet *bullet) {
  if (bullet_queue_is_full(bullet_queue)) bullet_dequeue(bullet_queue, bullet);

  bullet_queue->bullets[bullet_queue->back] = *bullet;
  bullet_queue->back = (bullet_queue->back + 1) % BULLET_AMOUNT;

  if (bullet_queue->size < BULLET_AMOUNT) { bullet_queue->size++; }
}

uint8_t bullet_timed_out(Bullet *bullet) {
  struct timeval now;
  uint32_t time_created_full, time_now_full;

  gettimeofday(&now, NULL);

  //Combine seconds & microseconds for a precise result
  //(microseconds loop otherwise)
  time_created_full = bullet->time_created.tv_sec * 1000000;
  time_created_full += bullet->time_created.tv_usec;

  time_now_full = now.tv_sec * 1000000 + now.tv_usec;

  if ((time_now_full - time_created_full) / 1000000 >= BULLET_TIMEOUT) return 1;
  return 0;
}

void shoot_bullet(Player *p, uint16_t pos_x, uint16_t pos_y, int16_t angle) {
  //Get player width & height
  int p_width, p_height;
  SDL_QueryTexture(p->texture, NULL, NULL, &p_width, &p_height);

  //Create bullet
  Bullet bullet = {0};
  if (!pos_x) pos_x = (uint16_t)p->pos_x + p_width / 2 - (BULLET_SIZE / 2 - 1);
  if (!pos_y) pos_y = (uint16_t)p->pos_y + p_height / 2 - (BULLET_SIZE / 2 - 1);
  if (!angle) angle = p->angle;

  bullet.pos_x = pos_x;
  bullet.pos_y = pos_y;
  bullet.angle = angle;

  gettimeofday(&bullet.time_created, NULL);
  bullet_enqueue(&p->bullet_queue, &bullet);

  //Broadcast to clients if server
  if (app.server) send_enet_host_new_bullet(p, &bullet);
}

Player *bullet_collided(Player *p, float *pos_x_bullet, float *pos_y_bullet) {
  //Check other player collisions
  for (int i = 0; i < app.num_of_players; i++) {
    if (p->id == app.players[i].id) continue; //Ignore self

    //Create other player rectangle
    uint16_t pos_x_other = app.players[i].pos_x;
    uint16_t pos_y_other = app.players[i].pos_y;
    SDL_Rect rect_other = {pos_x_other, pos_y_other, PLAYER_SIZE, PLAYER_SIZE};

    //Create tank rectangle
    SDL_Rect rect_bullet = {(uint16_t)*pos_x_bullet, (uint16_t)*pos_y_bullet,
                            BULLET_SIZE, BULLET_SIZE};

    if (SDL_HasIntersection(&rect_other, &rect_bullet) == SDL_TRUE)
      return &app.players[i];
  }

  return NULL;
}

void update_bullet_angle(Bullet *b, float *pos_x_bullet, float *pos_y_bullet,
                         SDL_Rect *rect_wall, uint16_t i, uint16_t j) {
  /* The bullet can either bounce on the x or y plane.
   * What we check is if bouncing on the x plane (360 - b->angle)
   * would result in a new collision. If it would, we instead
   * bounce on the y plane (180 - b->angle).
   *
   * We also need to check if the new position would collide with
   * the block to the right of the current one. This is due to
   * the collision check going from left to right and top to
   * bottom. A corner collision with a rectangle might result in an
   * x plane bounce but there might actually be a rectangle to
   * the right of it, meaning a y plane bounce is the correct
   * choice.
   */
  uint16_t new_angle = 360 - b->angle;
  float new_pos_x = *pos_x_bullet + sin(new_angle * PI/180) * BULLET_SPEED;
  float new_pos_y = *pos_y_bullet - cos(new_angle * PI/180) * BULLET_SPEED;

  //Create new bullet rectangle
  SDL_Rect new_rect_bullet = {(uint16_t)new_pos_x, (uint16_t)new_pos_y,
                              BULLET_SIZE, BULLET_SIZE};
  if (SDL_HasIntersection(rect_wall, &new_rect_bullet) == SDL_TRUE) {
    b->angle = 180 - b->angle;
  }
  else { //Check if there is a collision with a block to the right
    if (j + 1 < MAP_WIDTH && app.map[i][j + 1] == 1) {
      uint16_t pos_x_wall_right = (j + 1) * TILE_SIZE;
      uint16_t pos_y_wall_right = i * TILE_SIZE;
      SDL_Rect rect_wall_right = {pos_x_wall_right, pos_y_wall_right,
                                  TILE_SIZE, TILE_SIZE};

      if (SDL_HasIntersection(&rect_wall_right, &new_rect_bullet) == SDL_TRUE) {
        b->angle = 180 - b->angle;
      }
    }
    else {
      b->angle = 360 - b->angle;
    }
  }
}

void bullet_bounce(Bullet *b, float *pos_x_bullet, float *pos_y_bullet) {
  //Check map collisions
  for (uint16_t i = 0; i < MAP_HEIGHT; i++) {
    for (uint16_t j = 0; j < MAP_WIDTH; j++) {
      if (app.map[i][j] == 0) { continue; }

      //Create wall rectangle
      uint16_t pos_x_wall = j * TILE_SIZE;
      uint16_t pos_y_wall = i * TILE_SIZE;
      SDL_Rect rect_wall = {pos_x_wall, pos_y_wall, TILE_SIZE, TILE_SIZE};

      //Create bullet rectangle
      SDL_Rect rect_bullet = {(uint16_t)*pos_x_bullet,
                              (uint16_t) *pos_y_bullet,
                              BULLET_SIZE, BULLET_SIZE};

      if (SDL_HasIntersection(&rect_wall, &rect_bullet) == SDL_TRUE) {
        update_bullet_angle(b, pos_x_bullet, pos_y_bullet, &rect_wall, i, j);
        return;
      }
    }
  }
}

void update_bullet_positions(Player *p) {
  for (int i = 0; i < p->bullet_queue.size; i++) {
    int index = (p->bullet_queue.front + i) % BULLET_AMOUNT;
    Bullet *b = &p->bullet_queue.bullets[index];

    //Delete bullet if it timed out
    if (bullet_timed_out(b)) {
      bullet_dequeue(&p->bullet_queue, b);
      continue;
    }

    float new_pos_x = b->pos_x + sin(b->angle * PI/180) * BULLET_SPEED;
    float new_pos_y = b->pos_y - cos(b->angle * PI/180) * BULLET_SPEED;

    //Check if bullet hit a player
    Player *player_hit = bullet_collided(p, &new_pos_x, &new_pos_y);
    if (player_hit) {
      if (app.server) { send_enet_host_player_hit(player_hit, p); }
      bullet_dequeue(&p->bullet_queue, b);
      continue;
    }

    //Change angle of bullet if it hit a wall
    bullet_bounce(b, &new_pos_x, &new_pos_y);

    //Move bullet
    b->pos_x = new_pos_x;
    b->pos_y = new_pos_y;
  }
}

void drawBullets(Player *player) {
  for (int i = 0; i < player->bullet_queue.size; i++) {
    uint8_t index = (player->bullet_queue.front + i) % BULLET_AMOUNT;

    //Round positions to int
    uint16_t pos_x = player->bullet_queue.bullets[index].pos_x;
    uint16_t pos_y = player->bullet_queue.bullets[index].pos_y;

    //Draw rectangle
    SDL_Rect rect = {pos_x, pos_y, BULLET_SIZE, BULLET_SIZE};
    SDL_SetRenderDrawColor(app.renderer, 220, 0, 0, 255);
    SDL_RenderDrawRect(app.renderer, &rect);
  }
}

/* Game loop logic */
uint8_t load() {
  if (app.server) {
    generate_map();
    uint8_t res = create_player(&app.players[0], 0, 0, 0);
    if (res == EXIT_FAILURE) return EXIT_FAILURE;

    app.local_player = &app.players[0]; //Create a pointer to the local player
  }

  return 0;
}

void update() {
  if (!app.num_of_players) { return; } //Skip if no players
  int16_t *angle = &(app.local_player->angle);

  if (app.up) movePlayerForward(app.local_player);
  if (app.down) movePlayerBackward(app.local_player);
  if (app.left) *angle = (*angle - PLAYER_ROTATION_SPEED) % 360;
  if (app.right) *angle = (*angle + PLAYER_ROTATION_SPEED) % 360;
  if (app.button_a && !app.button_a_is_down) {
    shoot_bullet(app.local_player, 0, 0, 0);
    app.button_a_is_down = 1;
  }

  for (uint8_t i = 0; i < app.num_of_players; i++) {
    update_bullet_positions(&app.players[i]);
  }
}

void draw() {
  if (!app.num_of_players) { return; } //Skip if no players

  //Draw background
  SDL_SetRenderDrawColor(app.renderer, 25, 25, 25, 255);
  SDL_RenderClear(app.renderer);

  draw_map(); //Draw map

  for (int i = 0; i < app.num_of_players; i++) {
    drawPlayer(&app.players[i]); //Draw player
    drawBullets(&app.players[i]); //Draw bullets
  }

  //Present
  SDL_RenderPresent(app.renderer);
  SDL_Delay(16);
}

int main(int argc, char **argv) {
  app.is_running = 1;
  atexit(cleanup); //Assign a cleanup function
  if (init_SDL() == EXIT_FAILURE) return EXIT_FAILURE; //Initialize SDL
  if (init_enet() == EXIT_FAILURE) return EXIT_FAILURE; //Initialize ENet

  if (host_or_join(argv) == EXIT_FAILURE) return EXIT_FAILURE; //Host or join
  if (load() == EXIT_FAILURE) return EXIT_FAILURE; //Load state


  while (app.is_running) {
    poll_enet();
    poll_events();
    update();
    draw();
    send_enet();
  }

  return 0;
}
