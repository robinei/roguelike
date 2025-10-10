// WASM memory and game state
let memory = null;
let wasmExports = null;
let worldStatePtr = null;

// WebGL resources
let gl = null;
let shaderProgram = null;
let tileAtlas = null;
let fontAtlas = null;

// Game constants
const TILE_SIZE = 12;
const WORLD_STATE_SIZE = 1024 * 1024 * 4; // 4MB for world state

// Input command enum (must match C enum)
const InputCommand = {
  NONE: 0,
  UP: 1,
  UP_RIGHT: 2,
  RIGHT: 3,
  DOWN_RIGHT: 4,
  DOWN: 5,
  DOWN_LEFT: 6,
  LEFT: 7,
  UP_LEFT: 8,
  PERIOD: 9,
};

// Render command types
const RenderCommand = {
  TILE: 0,
  RECT: 1,
  LINE: 2,
};

// Atlas IDs
const AtlasId = {
  TILES: 0,
  FONT: 1,
};

// WebGL shader sources
const vertexShaderSource = `
  attribute vec2 a_position;
  attribute vec2 a_texCoord;

  uniform vec2 u_resolution;

  varying vec2 v_texCoord;

  void main() {
    vec2 clipSpace = (a_position / u_resolution) * 2.0 - 1.0;
    gl_Position = vec4(clipSpace.x, -clipSpace.y, 0, 1);
    v_texCoord = a_texCoord;
  }
`;

const fragmentShaderSource = `
  precision mediump float;

  uniform sampler2D u_texture;
  uniform vec4 u_color;
  uniform bool u_useTexture;

  varying vec2 v_texCoord;

  void main() {
    if (u_useTexture) {
      gl_FragColor = texture2D(u_texture, v_texCoord);
    } else {
      gl_FragColor = u_color;
    }
  }
`;

// Initialize WebGL context and shaders
function initWebGL(canvas) {
  gl = canvas.getContext('webgl', { alpha: false, antialias: false });
  if (!gl) {
    throw new Error('WebGL not supported');
  }

  // Create shaders
  const vertexShader = createShader(gl, gl.VERTEX_SHADER, vertexShaderSource);
  const fragmentShader = createShader(gl, gl.FRAGMENT_SHADER, fragmentShaderSource);

  // Create program
  shaderProgram = createProgram(gl, vertexShader, fragmentShader);

  // Get attribute/uniform locations
  shaderProgram.attribLocations = {
    position: gl.getAttribLocation(shaderProgram, 'a_position'),
    texCoord: gl.getAttribLocation(shaderProgram, 'a_texCoord'),
  };

  shaderProgram.uniformLocations = {
    resolution: gl.getUniformLocation(shaderProgram, 'u_resolution'),
    texture: gl.getUniformLocation(shaderProgram, 'u_texture'),
    color: gl.getUniformLocation(shaderProgram, 'u_color'),
    useTexture: gl.getUniformLocation(shaderProgram, 'u_useTexture'),
  };

  // Create buffers
  shaderProgram.positionBuffer = gl.createBuffer();
  shaderProgram.texCoordBuffer = gl.createBuffer();

  // Set up blending for transparency
  gl.enable(gl.BLEND);
  gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);

  return gl;
}

function createShader(gl, type, source) {
  const shader = gl.createShader(type);
  gl.shaderSource(shader, source);
  gl.compileShader(shader);

  if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
    console.error('Shader compile error:', gl.getShaderInfoLog(shader));
    gl.deleteShader(shader);
    return null;
  }

  return shader;
}

function createProgram(gl, vertexShader, fragmentShader) {
  const program = gl.createProgram();
  gl.attachShader(program, vertexShader);
  gl.attachShader(program, fragmentShader);
  gl.linkProgram(program);

  if (!gl.getProgramParameter(program, gl.LINK_STATUS)) {
    console.error('Program link error:', gl.getProgramInfoLog(program));
    gl.deleteProgram(program);
    return null;
  }

  return program;
}

// Load texture from image URL
function loadTexture(gl, url) {
  const texture = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_2D, texture);

  // Temporary 1x1 pixel while loading
  gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 1, 1, 0, gl.RGBA, gl.UNSIGNED_BYTE,
    new Uint8Array([255, 0, 255, 255]));

  const image = new Image();
  image.onload = () => {
    gl.bindTexture(gl.TEXTURE_2D, texture);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, image);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);

    texture.width = image.width;
    texture.height = image.height;

    console.log(`Loaded texture: ${url} (${image.width}x${image.height})`);
  };
  image.src = url;

  return texture;
}

// Draw a textured quad
function drawQuad(x, y, w, h, atlasX, atlasY, atlasW, atlasH, texture) {
  const positions = new Float32Array([
    x, y,
    x + w, y,
    x, y + h,
    x, y + h,
    x + w, y,
    x + w, y + h,
  ]);

  const texWidth = texture.width || 1;
  const texHeight = texture.height || 1;
  const u0 = atlasX / texWidth;
  const v0 = atlasY / texHeight;
  const u1 = (atlasX + atlasW) / texWidth;
  const v1 = (atlasY + atlasH) / texHeight;

  const texCoords = new Float32Array([
    u0, v0,
    u1, v0,
    u0, v1,
    u0, v1,
    u1, v0,
    u1, v1,
  ]);

  gl.bindBuffer(gl.ARRAY_BUFFER, shaderProgram.positionBuffer);
  gl.bufferData(gl.ARRAY_BUFFER, positions, gl.STATIC_DRAW);
  gl.enableVertexAttribArray(shaderProgram.attribLocations.position);
  gl.vertexAttribPointer(shaderProgram.attribLocations.position, 2, gl.FLOAT, false, 0, 0);

  gl.bindBuffer(gl.ARRAY_BUFFER, shaderProgram.texCoordBuffer);
  gl.bufferData(gl.ARRAY_BUFFER, texCoords, gl.STATIC_DRAW);
  gl.enableVertexAttribArray(shaderProgram.attribLocations.texCoord);
  gl.vertexAttribPointer(shaderProgram.attribLocations.texCoord, 2, gl.FLOAT, false, 0, 0);

  gl.bindTexture(gl.TEXTURE_2D, texture);
  gl.uniform1i(shaderProgram.uniformLocations.useTexture, 1);

  gl.drawArrays(gl.TRIANGLES, 0, 6);
}

// Draw a colored rect
function drawRect(x, y, w, h, r, g, b, a) {
  const positions = new Float32Array([
    x, y,
    x + w, y,
    x, y + h,
    x, y + h,
    x + w, y,
    x + w, y + h,
  ]);

  gl.bindBuffer(gl.ARRAY_BUFFER, shaderProgram.positionBuffer);
  gl.bufferData(gl.ARRAY_BUFFER, positions, gl.STATIC_DRAW);
  gl.enableVertexAttribArray(shaderProgram.attribLocations.position);
  gl.vertexAttribPointer(shaderProgram.attribLocations.position, 2, gl.FLOAT, false, 0, 0);

  gl.disableVertexAttribArray(shaderProgram.attribLocations.texCoord);

  gl.uniform1i(shaderProgram.uniformLocations.useTexture, 0);
  gl.uniform4f(shaderProgram.uniformLocations.color, r / 255, g / 255, b / 255, a / 255);

  gl.drawArrays(gl.TRIANGLES, 0, 6);
}

// Execute command buffer from WASM
function executeRenderCommands(bufferPtr, count) {
  const TILE_PADDING = 1;
  const tileAtlasCols = Math.floor((tileAtlas.width - TILE_PADDING) / (TILE_SIZE + TILE_PADDING));

  // Command buffer layout: types[512], data[512*6]
  const typesPtr = bufferPtr;
  const dataPtr = bufferPtr + 512;

  for (let i = 0; i < count; i++) {
    const type = new Uint8Array(memory.buffer, typesPtr + i, 1)[0];
    const dataOffset = dataPtr + i * 6 * 4; // 6 int32s per command
    const data = new Int32Array(memory.buffer, dataOffset, 6);

    if (type === RenderCommand.TILE) {
      const atlasId = data[0];
      const tileIndex = data[1];
      const x = data[2];
      const y = data[3];
      const w = data[4];
      const h = data[5];

      const texture = atlasId === AtlasId.TILES ? tileAtlas : fontAtlas;
      const cols = atlasId === AtlasId.TILES ? tileAtlasCols : 16;

      const tileX = tileIndex % cols;
      const tileY = Math.floor(tileIndex / cols);

      let atlasX, atlasY;
      if (atlasId === AtlasId.TILES) {
        atlasX = TILE_PADDING + tileX * (TILE_SIZE + TILE_PADDING);
        atlasY = TILE_PADDING + tileY * (TILE_SIZE + TILE_PADDING);
      } else {
        atlasX = tileX * TILE_SIZE;
        atlasY = tileY * TILE_SIZE;
      }

      drawQuad(x, y, w, h, atlasX, atlasY, TILE_SIZE, TILE_SIZE, texture);
    } else if (type === RenderCommand.RECT) {
      const x = data[0];
      const y = data[1];
      const w = data[2];
      const h = data[3];
      const color = data[4] >>> 0; // Unsigned

      const r = (color >> 24) & 0xFF;
      const g = (color >> 16) & 0xFF;
      const b = (color >> 8) & 0xFF;
      const a = color & 0xFF;

      drawRect(x, y, w, h, r, g, b, a);
    } else if (type === RenderCommand.LINE) {
      // Line rendering not yet implemented
      console.warn('Line rendering not implemented');
    }
  }
}

// Log levels matching C enum
const LogLevel = {
  DEBUG: 0,
  LOG: 1,
  INFO: 2,
  WARN: 3,
  ERROR: 4,
};

// Read a null-terminated string from WASM memory
function readString(ptr) {
  const bytes = new Uint8Array(memory.buffer);
  let end = ptr;
  while (bytes[end] !== 0) end++;
  return new TextDecoder().decode(bytes.subarray(ptr, end));
}

// WASM imports (functions the WASM module needs)
const wasmImports = {
  env: {
    // Math functions
    sin: Math.sin,
    cos: Math.cos,
    sqrt: Math.sqrt,
    atan2: Math.atan2,

    // Logging from WASM
    js_log: (level, messagePtr) => {
      const message = readString(messagePtr);
      switch (level) {
        case LogLevel.DEBUG:
          console.debug('[WASM]', message);
          break;
        case LogLevel.LOG:
          console.log('[WASM]', message);
          break;
        case LogLevel.INFO:
          console.info('[WASM]', message);
          break;
        case LogLevel.WARN:
          console.warn('[WASM]', message);
          break;
        case LogLevel.ERROR:
          console.error('[WASM]', message);
          break;
        default:
          console.log('[WASM]', message);
      }
    },

    // Render callback - called from game_render
    execute_render_commands: (implDataPtr, commandBufferPtr) => {
      // Read command buffer structure from WASM memory
      // CommandBuffer { int count; uint8_t types[512]; int32_t data[512*6]; }
      const countView = new Int32Array(memory.buffer, commandBufferPtr, 1);
      const count = countView[0];

      if (count > 0) {
        executeRenderCommands(commandBufferPtr + 4, count); // +4 to skip count field
      }
    },
  }
};

// Initialize WASM module
async function initWasm() {
  // Create memory (4MB)
  memory = new WebAssembly.Memory({ initial: 256 }); // 256 pages = 16MB
  wasmImports.env.memory = memory;

  // Load and instantiate WASM module
  const response = await fetch('game.wasm');
  const wasmBytes = await response.arrayBuffer();
  const wasmModule = await WebAssembly.instantiate(wasmBytes, wasmImports);

  wasmExports = wasmModule.instance.exports;

  // Allocate WorldState at the beginning of memory
  worldStatePtr = 0;

  console.log('WASM module loaded');
  console.log('Exported functions:', Object.keys(wasmExports));

  // Initialize the game
  wasmExports.game_init(worldStatePtr);

  return wasmExports;
}

// Input handling
function setupInput() {
  const keyMap = {
    'ArrowUp': InputCommand.UP,
    'k': InputCommand.UP,
    'ArrowDown': InputCommand.DOWN,
    'j': InputCommand.DOWN,
    'ArrowLeft': InputCommand.LEFT,
    'h': InputCommand.LEFT,
    'ArrowRight': InputCommand.RIGHT,
    'l': InputCommand.RIGHT,
    'y': InputCommand.UP_LEFT,
    'u': InputCommand.UP_RIGHT,
    'b': InputCommand.DOWN_LEFT,
    'n': InputCommand.DOWN_RIGHT,
    '.': InputCommand.PERIOD,
  };

  document.addEventListener('keydown', (e) => {
    const cmd = keyMap[e.key];
    if (cmd !== undefined) {
      e.preventDefault();
      wasmExports.game_input(worldStatePtr, cmd);
    }
  });
}

// Main game loop
let lastTime = 0;
function gameLoop(currentTime) {
  const dt = (currentTime - lastTime) / 1000.0; // Convert to seconds
  lastTime = currentTime;

  // Update game
  wasmExports.game_frame(worldStatePtr, dt);

  // Clear screen
  gl.clearColor(0, 0, 0, 1);
  gl.clear(gl.COLOR_BUFFER_BIT);

  gl.useProgram(shaderProgram);
  gl.uniform2f(shaderProgram.uniformLocations.resolution, gl.canvas.width, gl.canvas.height);

  // Render game (calls back into JS via execute_render_commands)
  const tileSize = TILE_SIZE * 2; // 2x scaling
  wasmExports.game_render_wasm(worldStatePtr, gl.canvas.width, gl.canvas.height, tileSize);

  requestAnimationFrame(gameLoop);
}

// Main initialization
async function main() {
  const canvas = document.getElementById('canvas');
  const info = document.getElementById('info');

  try {
    info.textContent = 'Initializing WebGL...';
    initWebGL(canvas);

    info.textContent = 'Loading textures...';
    tileAtlas = loadTexture(gl, 'urizen_onebit_tileset__v2d0.png');
    fontAtlas = loadTexture(gl, 'cp437_12x12.png');

    info.textContent = 'Loading WASM...';
    await initWasm();

    info.textContent = 'Setting up input...';
    setupInput();

    info.textContent = 'Running...';
    lastTime = performance.now();
    requestAnimationFrame(gameLoop);

  } catch (error) {
    console.error('Initialization error:', error);
    info.textContent = 'Error: ' + error.message;
  }
}

// Start when page loads
window.addEventListener('load', main);
