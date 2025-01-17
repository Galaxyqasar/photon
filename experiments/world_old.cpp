
class World {
public:
	World(const std::string &shader, Camera &cam);
	~World();

	void loadShader(const std::string &path);
	void init();

	void setCameraHostPtr(const std::shared_ptr<Entity> &host);
	void setTexturePtr(const std::shared_ptr<TiledTexture> &texture);
	void setParticleTexturePtr(const std::shared_ptr<TiledTexture> &texture);

	Chunk* createChunk(lvec2 pos);
	Chunk* getChunk(lvec2 pos);
	const Chunk* getChunk(lvec2 pos) const;

	Chunk* generateFlatChunk(lvec2 pos);

	template<typename T, typename ...Args>
	std::shared_ptr<T> createEntity(Args ...args) {
		std::shared_ptr<T> entity = std::shared_ptr<T>(new T(args...));
		entities.push_back(entity);
		return entity;
	}

	std::shared_ptr<TextObject> createTextObject(const std::string &text, const mat4 &transform, vec4 color = vec4(1));

	void save(const std::string &path) const;
	void load(const std::string &path);

	void setAutoGrow(bool state);
	bool getAutoGrow();

	void update(float time, float dt);
	void render();
	void renderCollisions(std::vector<ivec2> tiles, std::shared_ptr<TiledTexture> texture = {});

	void shift(ivec2 dir);
	lvec2 getShiftOffset() const;

	Tile& operator[](const ivec2 &pos);
	const Tile& operator[](const ivec2 &pos) const;
	Tile& at(const ivec2 &pos);
	const Tile& at(const ivec2 &pos) const;

	Tile getTileOrEmpty(const ivec2 &pos) const;

	vec2 snapToGrid(vec2 worldpos) const;
	ivec2 getTileIndex(vec2 worldpos) const;

	void createBloodParticles(vec2 pos);

	struct ModelInfo {
		mat4 transform, uvtransform;
	};

	struct CameraInfo {
		mat4 proj, view;
	};

	struct RenderInfo {
		vec4 tint;
		vec2 res;
		float time, dt;
	};

private:
	bool autogrow = false;
	opengl::Mesh<vec3, vec2> unitplane;
	opengl::UniformBuffer<ModelInfo> modelInfoUBO;
	opengl::UniformBuffer<CameraInfo> cameraInfoUBO;
	opengl::UniformBuffer<RenderInfo> renderInfoUBO;
	opengl::Program shader;

	std::vector<std::unique_ptr<Chunk>> chunks;
	std::vector<std::shared_ptr<Entity>> entities;
	std::shared_ptr<TiledTexture> texture;

	Camera &cam;
	std::mutex cameraMutex;
	std::shared_ptr<Entity> camHost;

	ParticleSystem particleSystem;
	TextRenderer textRenderer;

	Tile fallback;

	ivec2 shiftOffset = 0;
};


World::World(const std::string &path, Camera &cam) : cam(cam), textRenderer(freetype::Font("res/jetbrains-mono.ttf")) {
	loadShader(path);
	init();
}

World::~World() {}

void World::loadShader(const std::string &path) {
	std::ifstream src(path, std::ios::ate);
	std::string buffer(src.tellg(), '\0');
	src.seekg(src.beg);
	src.read(buffer.data(), buffer.size());

	shader = opengl::Program::load(buffer, opengl::Shader::VertexStage | opengl::Shader::FragmentStage);
	shader.use();
	shader.setUniform("sampler", 0);
}

void World::init() {
	shader.use();
	// init ubos
	cameraInfoUBO.bindBase(0);
	cameraInfoUBO.setData({mat4(), mat4()});

	modelInfoUBO.bindBase(1);
	modelInfoUBO.setData({mat4(), mat4()});

	renderInfoUBO.bindBase(2);
	renderInfoUBO.setData({vec4(0), cam.res, 0.0f, 0.0f});

	// init meshes
	unitplane = opengl::Mesh<vec3, vec2>({
		{ math::vec3( 0.5f, 0.5f, 0.0f), math::vec2(1,0) },
		{ math::vec3(-0.5f, 0.5f, 0.0f), math::vec2(0,0) },
		{ math::vec3(-0.5f,-0.5f, 0.0f), math::vec2(0,1) },
		{ math::vec3( 0.5f,-0.5f, 0.0f), math::vec2(1,1) },
	}, {
		0, 1, 2, 2, 3, 0,
	});
}

void World::setTexturePtr(const std::shared_ptr<TiledTexture> &texture) {
	this->texture = texture;
}

void World::setCameraHostPtr(const std::shared_ptr<Entity> &host) {
	std::lock_guard<std::mutex> lock(cameraMutex);
	camHost = host;
}

void World::setParticleTexturePtr(const std::shared_ptr<TiledTexture> &texture) {
	particleSystem.setTexture(texture);
}

Chunk* World::createChunk(lvec2 pos) {
	chunks.push_back(std::unique_ptr<Chunk>(new Chunk(this, pos, texture->scale())));
	return chunks.back().get();
}

Chunk* World::getChunk(lvec2 pos) {
	for(const auto &chunk : chunks) {
		if(chunk->getPos() == pos) {
			return chunk.get();
		}
	}
	if(autogrow) {
		return createChunk(pos);
	}
	return nullptr;
}

const Chunk* World::getChunk(lvec2 pos) const {
	for(const auto &chunk : chunks) {
		if(chunk->getPos() == pos) {
			return chunk.get();
		}
	}
	return nullptr;
}

Chunk* World::generateFlatChunk(lvec2 pos) {
	Chunk *chunk = createChunk(pos);
	chunk->fill(Tile::rock);

	for(int x = 0; x < Chunk::size; x++) {
		chunk->at(ivec2(x, Chunk::size - 1)) = Tile::grass;
		chunk->at(ivec2(x, Chunk::size - 2)) = Tile::grass;
		for(int y = 0; y < 10; y++) {
			chunk->at(ivec2(x, Chunk::size - 3 - y)) = Tile(Tile::dirt, y);
		}
		for(int y = 0; y < 10; y++) {
			chunk->at(ivec2(x, Chunk::size - 13 - y)) = Tile(Tile::stone, y);
		}
	}

	return chunk;
}

std::shared_ptr<TextObject> World::createTextObject(const std::string &text, const mat4 &transform, vec4 color) {
	return textRenderer.createObject(text, transform, color);
}

void World::setAutoGrow(bool state) {
	autogrow = state;
}

bool World::getAutoGrow() {
	return autogrow;
}

void World::update(float time, float dt) {
	cameraMutex.lock();
	ivec2 shiftDir;

	if(camHost->pos.x < 0.0f) {
		camHost->shift(ivec2(1, 0));
		shiftDir += ivec2(1, 0);
	}
	else if(camHost->pos.x > Chunk::size * Tile::resolution) {
		camHost->shift(ivec2(-1, 0));
		shiftDir += ivec2(-1, 0);
	}

	if(camHost->pos.y < 0.0f) {
		camHost->shift(ivec2(0, 1));
		shiftDir += ivec2(0, 1);
	}
	else if(camHost->pos.y > Chunk::size * Tile::resolution) {
		camHost->shift(ivec2(0, -1));
		shiftDir += ivec2(0, -1);
	}

	camHost->update(time, dt, this);

	cameraMutex.unlock();

	shift(shiftDir);

	for(auto &chunk : chunks) {
		chunk->update(time, dt);
	}

	for(auto &entity : entities) {
		entity->update(time, dt, this);
	}

	if(particleSystem.size() < 8192) {
		for(unsigned i = 0; i < 10; i++) {
			vec2 pos = vec2(rand(-1024, 1536), rand(256, 512));
			vec2 scale = vec2(1, 8) * rand(0.8, 1.2);
			vec2 speed = vec2(0.0, rand(-112, -96));
			vec2 gravity = vec2(0, -1);
			particleSystem.spawn(Particle(Particle::rain, pos, speed, gravity, scale, 0, 0));
		}
	}
	for(Particle &p : particleSystem) {
		switch(p.type) {
			case Particle::rain: {
				if(p.speed.y == 0.0f || p.pos.y < -512.0f) {
					p.pos = vec2(rand(-1024, 1536), 512);
					p.speed = vec2(0.0, rand(-112, -96));
				}
			} break;
		}
	}
	particleSystem.erase([](const Particle &p) -> bool {
		if(p.type == Particle::blood && p.lifetime > 10) {
			return true;
		}
		return false;
	});

	particleSystem.update(time, dt, this);
	textRenderer.update();
}

void World::render() {
	shader.use();

	cameraInfoUBO.bindBase(0);
	modelInfoUBO.bindBase(1);
	renderInfoUBO.bindBase(2);

	cameraMutex.lock();

	mat4 proj = cam.proj();
	mat4 view = cam.view();
	vec2 res = cam.res;
	vec3 campos = cam.pos;

	cameraInfoUBO.update({proj, view});
	renderInfoUBO.update({vec4(0), res, 0.0f, 0.0f});

	mat4 transform = camHost->getTransform();
	vec2 pos = transform * vec4(0.0f, 0.0f, 0.0f, 1.0f);
	if(dist(campos.xy, pos) < Chunk::size * Tile::resolution * 2) {
		modelInfoUBO.update({transform, camHost->getUVTransform()});
		camHost->getTexturePtr()->activate();
		unitplane.drawElements();
	}

	cameraMutex.unlock();

	texture->activate();
	for(auto &chunk : chunks) {
		ivec2 chunkid = chunk->getPos();
		vec2 chunkpos = chunkid * Chunk::size * Tile::resolution;
		vec2 chunkcenter = (vec2(chunkid) + 0.5f) * Chunk::size * Tile::resolution;

		if(dist(campos.xy, chunkcenter) < Chunk::size * Tile::resolution * 1.5) {
			modelInfoUBO.update({mat4().translate(vec3(chunkpos)), mat4()});
			chunk->render();
		}
	}

	for(auto &entity : entities) {
		mat4 transform = entity->getTransform();
		vec2 pos = transform * vec4(0.0f, 0.0f, 0.0f, 1.0f);
		if(dist(campos.xy, pos) < Chunk::size * Tile::resolution * 2) {
			modelInfoUBO.update({transform, entity->getUVTransform()});
			entity->getTexturePtr()->activate();
			unitplane.drawElements();
		}
	}

	particleSystem.render(proj * view);
	textRenderer.render(proj * view);
}

void World::renderCollisions(std::vector<ivec2> tiles, std::shared_ptr<TiledTexture> texture) {
	shader.use();
	if(texture) {
		texture->activate();
	}

	cameraInfoUBO.bindBase(0);
	modelInfoUBO.bindBase(1);
	renderInfoUBO.bindBase(2);

	mat4 proj = cam.proj();
	mat4 view = cam.view();
	vec2 res = cam.res;

	cameraInfoUBO.update({proj, view});
	renderInfoUBO.setData({vec4(0.6f, 0.0f, 0.0f, 1.0f), res, 0.0f, 0.0f});

	for(ivec2 tile : tiles) {
		mat4 transform = mat4().translate(vec3(vec2(tile) + 0.5f, 0.0f)).scale(0.666f);
		modelInfoUBO.update({transform, mat4()});
		unitplane.drawElements();
	}
}

void World::shift(ivec2 dir) {
	for(auto &chunk : chunks) {
		chunk->shift(dir);
	}
	for(auto &entity : entities) {
		entity->shift(dir);
	}
	particleSystem.shift(dir);
	shiftOffset += dir;
}

lvec2 World::getShiftOffset() const {
	return shiftOffset;
}

Tile& World::operator[](const ivec2 &pos) {
	ivec2 chunkpos = pos / ivec2(Chunk::size) - ivec2(pos.x < 0, pos.y < 0);
	ivec2 tilepos = (pos - chunkpos * Chunk::size) % Chunk::size;
	Chunk *chunk = getChunk(chunkpos);
	if(!chunk) {
		throw std::runtime_error("no chunk at (" + std::to_string(pos.x) + ", " + std::to_string(pos.y) + ")!");
	}
	return chunk->at(tilepos);
}

const Tile& World::operator[](const ivec2 &pos) const {
	ivec2 chunkpos = pos / ivec2(Chunk::size) - ivec2(pos.x < 0, pos.y < 0);
	ivec2 tilepos = (pos - chunkpos * Chunk::size) % Chunk::size;
	const Chunk *chunk = getChunk(chunkpos);
	if(chunk) {
		return chunk->at(tilepos);
	}
	return fallback;
}

Tile& World::at(const ivec2 &pos) {
	return this->operator[](pos);
}

const Tile& World::at(const ivec2 &pos) const {
	return this->operator[](pos);
}

Tile World::getTileOrEmpty(const ivec2 &pos) const {
	Tile tile;
	try {
		tile = this->operator[](pos);
	} catch(std::exception &e) {}
	return tile;
}

vec2 World::snapToGrid(vec2 worldpos) const {
	return ivec2(worldpos) - ivec2(worldpos.x < 0 ? 1 : 0, worldpos.y < 0 ? 1 : 0);
}

ivec2 World::getTileIndex(vec2 worldpos) const {
	return snapToGrid(worldpos / Tile::resolution);
}

void World::createBloodParticles(vec2 pos) {
	for(int i = 0; i < 10; i++) {
		vec2 speed = vec2(rand(-32, 32), rand(-32, 32));
		float rspeed = rand(2, 4);
		rspeed *= ((speed.x > 0) ? 1 : -1);
		particleSystem.spawn(Particle(Particle::blood, pos + 0.5, speed, vec2(0, -128), vec2(2.0f), 0, rspeed));
	}
}