// WASM memory and game state
let memory = null;
let wasmExports = null;
let worldStatePtr = null;

// WebGL resources
let gl = null;
let shaderProgram = null;
let tileAtlas = null;

// Game constants
const TILE_SIZE = 12;
const WORLD_STATE_SIZE = 1024 * 1024 * 16; // 16MB for world state (more than enough)

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

// WebGL shader sources
const vertexShaderSource = `
  attribute vec2 a_position;
  attribute vec2 a_texCoord;
  attribute vec4 a_color;

  uniform vec2 u_resolution;

  varying vec2 v_texCoord;
  varying vec4 v_color;

  void main() {
    vec2 clipSpace = (a_position / u_resolution) * 2.0 - 1.0;
    gl_Position = vec4(clipSpace.x, -clipSpace.y, 0, 1);
    v_texCoord = a_texCoord;
    v_color = a_color;
  }
`;

const fragmentShaderSource = `
  precision mediump float;

  uniform sampler2D u_texture;

  varying vec2 v_texCoord;
  varying vec4 v_color;

  void main() {
    gl_FragColor = texture2D(u_texture, v_texCoord) * v_color;
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
    color: gl.getAttribLocation(shaderProgram, 'a_color'),
  };

  shaderProgram.uniformLocations = {
    resolution: gl.getUniformLocation(shaderProgram, 'u_resolution'),
    texture: gl.getUniformLocation(shaderProgram, 'u_texture'),
  };

  // Create single vertex buffer for interleaved data
  shaderProgram.vertexBuffer = gl.createBuffer();

  // Set up blending for transparency
  gl.enable(gl.BLEND);
  gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);

  return gl;

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
}

// Load texture from image URL
async function loadTexture(gl, url) {
  const texture = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_2D, texture);

  // Temporary 1x1 pixel while loading
  gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 1, 1, 0, gl.RGBA, gl.UNSIGNED_BYTE,
    new Uint8Array([255, 0, 255, 255]));

  // Load image and wait for it
  const image = await new Promise((resolve, reject) => {
    const img = new Image();
    img.onload = () => resolve(img);
    img.onerror = reject;
    img.src = url;
  });

  // Upload texture
  gl.bindTexture(gl.TEXTURE_2D, texture);
  gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, image);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);

  texture.width = image.width;
  texture.height = image.height;

  console.log(`Loaded texture: ${url} (${image.width}x${image.height})`);

  return texture;
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
    js_log(level, messagePtr) {
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

    // Submit geometry from WASM - called by game code (game_render)
    // Vertices are in interleaved format: position(2), color(4), tex_coord(2) = 8 floats = 32 bytes
    submit_geometry(implDataPtr, verticesPtr, vertexCount) {
      if (vertexCount === 0) return;

      // Read vertex data directly from WASM memory
      // Each vertex is 8 floats (32 bytes): [x, y, r, g, b, a, u, v]
      const floatsPerVertex = 8;
      const vertexData = new Float32Array(
        memory.buffer,
        verticesPtr,
        vertexCount * floatsPerVertex
      );

      // Bind texture
      gl.bindTexture(gl.TEXTURE_2D, tileAtlas);

      // Upload vertex data as interleaved buffer
      gl.bindBuffer(gl.ARRAY_BUFFER, shaderProgram.vertexBuffer);
      gl.bufferData(gl.ARRAY_BUFFER, vertexData, gl.STREAM_DRAW);

      const stride = floatsPerVertex * 4; // 32 bytes per vertex

      // Position attribute (offset 0)
      gl.enableVertexAttribArray(shaderProgram.attribLocations.position);
      gl.vertexAttribPointer(
        shaderProgram.attribLocations.position,
        2,
        gl.FLOAT,
        false,
        stride,
        0
      );

      // Color attribute (offset 8 bytes)
      gl.enableVertexAttribArray(shaderProgram.attribLocations.color);
      gl.vertexAttribPointer(
        shaderProgram.attribLocations.color,
        4,
        gl.FLOAT,
        false,
        stride,
        8
      );

      // Tex coord attribute (offset 24 bytes)
      gl.enableVertexAttribArray(shaderProgram.attribLocations.texCoord);
      gl.vertexAttribPointer(
        shaderProgram.attribLocations.texCoord,
        2,
        gl.FLOAT,
        false,
        stride,
        24
      );

      // Draw
      gl.drawArrays(gl.TRIANGLES, 0, vertexCount);
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
  heapBase = wasmExports.get_heap_base();

  worldStatePtr = heapBase;

  console.log('WASM module loaded. Heap base:', heapBase);

  // Initialize the game
  const rngSeed = BigInt(Math.floor(Math.random() * 0xffffffff));
  console.log('RNG seed:', rngSeed)
  wasmExports.game_init(worldStatePtr, rngSeed);

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

  // Render game (calls back into JS via submit_geometry)
  const tileSizeScaled = TILE_SIZE * 2; // 2x scaling
  wasmExports.game_render_wasm(
    worldStatePtr,
    gl.canvas.width,
    gl.canvas.height,
    tileSizeScaled,
    tileAtlas.width,
    tileAtlas.height
  );

  requestAnimationFrame(gameLoop);
}

// Resize canvas to match physical pixels
function resizeCanvas() {
  const canvas = document.getElementById('canvas');
  canvas.width = canvas.clientWidth;
  canvas.height = canvas.clientHeight;
  console.log('Resized canvas to', canvas.width, '*', canvas.height)
  if (gl) {
    gl.viewport(0, 0, canvas.width, canvas.height);
  }
}

// Main initialization
async function main() {
  const canvas = document.getElementById('canvas');
  const info = document.getElementById('info');

  try {
    info.textContent = 'Initializing WebGL...';
    resizeCanvas();
    initWebGL(canvas);

    info.textContent = 'Loading textures...';
    tileAtlas = await loadTexture(gl, 'combined_tileset.png');

    info.textContent = 'Loading WASM...';
    await initWasm();

    info.textContent = 'Setting up input...';
    setupInput();

    // Handle window resize
    window.addEventListener('resize', resizeCanvas);

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
