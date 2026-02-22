import type { VideoFrame } from '../decoder/types.js';

const VERTEX_SHADER_SOURCE = `
attribute vec2 a_position;
attribute vec2 a_texCoord;
varying vec2 v_texCoord;

void main() {
    gl_Position = vec4(a_position, 0.0, 1.0);
    v_texCoord = a_texCoord;
}
`;

const FRAGMENT_SHADER_SOURCE = `
precision mediump float;
varying vec2 v_texCoord;
uniform sampler2D u_textureY;
uniform sampler2D u_textureU;
uniform sampler2D u_textureV;
uniform float u_xScale;

void main() {
    vec2 coord = vec2(v_texCoord.x * u_xScale, v_texCoord.y);
    float y = texture2D(u_textureY, coord).r;
    float u = texture2D(u_textureU, coord).r - 0.5;
    float v = texture2D(u_textureV, coord).r - 0.5;

    // BT.601
    float r = y + 1.402 * v;
    float g = y - 0.344136 * u - 0.714136 * v;
    float b = y + 1.772 * u;

    gl_FragColor = vec4(r, g, b, 1.0);
}
`;

export class WebGLRenderer {
    private gl: WebGLRenderingContext;
    private program: WebGLProgram;
    private textureY: WebGLTexture;
    private textureU: WebGLTexture;
    private textureV: WebGLTexture;
    private xScaleLocation: WebGLUniformLocation;
    private lastWidth: number = 0;
    private lastHeight: number = 0;
    private contextLost: boolean = false;

    constructor(canvas: HTMLCanvasElement) {
        const gl = canvas.getContext('webgl', {
            preserveDrawingBuffer: false,
            antialias: false,
            depth: false,
            stencil: false,
        });
        if (!gl) {
            throw new Error('WebGL not available');
        }
        this.gl = gl;

        canvas.addEventListener('webglcontextlost', (e) => {
            e.preventDefault();
            this.contextLost = true;
        });

        this.program = this.createProgram();
        this.textureY = this.createTexture(0);
        this.textureU = this.createTexture(1);
        this.textureV = this.createTexture(2);
        this.xScaleLocation = gl.getUniformLocation(this.program, 'u_xScale')!;
        this.setupGeometry();
    }

    isAvailable(): boolean {
        return !this.contextLost;
    }

    renderFrame(frame: VideoFrame): void {
        if (this.contextLost) return;

        const gl = this.gl;
        const canvas = gl.canvas as HTMLCanvasElement;

        if (canvas.width !== frame.width || canvas.height !== frame.height) {
            canvas.width = frame.width;
            canvas.height = frame.height;
            gl.viewport(0, 0, frame.width, frame.height);
        }

        const needRealloc =
            frame.width !== this.lastWidth || frame.height !== this.lastHeight;

        this.uploadTexture(
            0,
            this.textureY,
            frame.yStride,
            frame.height,
            frame.yData,
            needRealloc
        );
        this.uploadTexture(
            1,
            this.textureU,
            frame.uStride,
            frame.height >> 1,
            frame.uData,
            needRealloc
        );
        this.uploadTexture(
            2,
            this.textureV,
            frame.vStride,
            frame.height >> 1,
            frame.vData,
            needRealloc
        );

        // width/stride ratio to only sample valid pixels
        gl.uniform1f(this.xScaleLocation, frame.width / frame.yStride);

        if (needRealloc) {
            this.lastWidth = frame.width;
            this.lastHeight = frame.height;
        }

        gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
    }

    destroy(): void {
        const gl = this.gl;
        if (this.contextLost) return;
        gl.deleteTexture(this.textureY);
        gl.deleteTexture(this.textureU);
        gl.deleteTexture(this.textureV);
        gl.deleteProgram(this.program);
    }

    private createProgram(): WebGLProgram {
        const gl = this.gl;
        const vs = this.compileShader(gl.VERTEX_SHADER, VERTEX_SHADER_SOURCE);
        const fs = this.compileShader(
            gl.FRAGMENT_SHADER,
            FRAGMENT_SHADER_SOURCE
        );

        const program = gl.createProgram()!;
        gl.attachShader(program, vs);
        gl.attachShader(program, fs);
        gl.linkProgram(program);

        if (!gl.getProgramParameter(program, gl.LINK_STATUS)) {
            const info = gl.getProgramInfoLog(program);
            gl.deleteProgram(program);
            throw new Error(`Program link failed: ${info}`);
        }

        gl.deleteShader(vs);
        gl.deleteShader(fs);
        gl.useProgram(program);

        // Bind texture units to uniform samplers
        gl.uniform1i(gl.getUniformLocation(program, 'u_textureY'), 0);
        gl.uniform1i(gl.getUniformLocation(program, 'u_textureU'), 1);
        gl.uniform1i(gl.getUniformLocation(program, 'u_textureV'), 2);

        return program;
    }

    private compileShader(type: number, source: string): WebGLShader {
        const gl = this.gl;
        const shader = gl.createShader(type)!;
        gl.shaderSource(shader, source);
        gl.compileShader(shader);

        if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
            const info = gl.getShaderInfoLog(shader);
            gl.deleteShader(shader);
            throw new Error(`Shader compile failed: ${info}`);
        }

        return shader;
    }

    private createTexture(unit: number): WebGLTexture {
        const gl = this.gl;
        const texture = gl.createTexture()!;
        gl.activeTexture(gl.TEXTURE0 + unit);
        gl.bindTexture(gl.TEXTURE_2D, texture);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
        return texture;
    }

    private uploadTexture(
        unit: number,
        texture: WebGLTexture,
        width: number,
        height: number,
        data: Uint8Array,
        realloc: boolean
    ): void {
        const gl = this.gl;
        gl.activeTexture(gl.TEXTURE0 + unit);
        gl.bindTexture(gl.TEXTURE_2D, texture);

        if (realloc) {
            gl.texImage2D(
                gl.TEXTURE_2D,
                0,
                gl.LUMINANCE,
                width,
                height,
                0,
                gl.LUMINANCE,
                gl.UNSIGNED_BYTE,
                data
            );
        } else {
            gl.texSubImage2D(
                gl.TEXTURE_2D,
                0,
                0,
                0,
                width,
                height,
                gl.LUMINANCE,
                gl.UNSIGNED_BYTE,
                data
            );
        }
    }

    private setupGeometry(): void {
        const gl = this.gl;

        // Full-screen quad: position + texCoord interleaved
        // Positions: clip space, TexCoords: flipped Y (top-left origin for video)
        const vertices = new Float32Array([
            // x,    y,    s,   t
            -1.0, -1.0, 0.0, 1.0, 1.0, -1.0, 1.0, 1.0, -1.0, 1.0, 0.0, 0.0,
            1.0, 1.0, 1.0, 0.0,
        ]);

        const buffer = gl.createBuffer();
        gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
        gl.bufferData(gl.ARRAY_BUFFER, vertices, gl.STATIC_DRAW);

        const posLoc = gl.getAttribLocation(this.program, 'a_position');
        gl.enableVertexAttribArray(posLoc);
        gl.vertexAttribPointer(posLoc, 2, gl.FLOAT, false, 16, 0);

        const texLoc = gl.getAttribLocation(this.program, 'a_texCoord');
        gl.enableVertexAttribArray(texLoc);
        gl.vertexAttribPointer(texLoc, 2, gl.FLOAT, false, 16, 8);
    }
}
