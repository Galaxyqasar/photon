#include <world.hpp>

#include <algorithm>
#include <fstream>

void WorldContainer::setChunk(lvec2 pos, const std::shared_ptr<Chunk> &chunk) {
	m_chunks[pos + m_offset] = chunk;
}

std::shared_ptr<Chunk> WorldContainer::getChunk(lvec2 pos) {
	return m_chunks[pos + m_offset];
}

std::shared_ptr<Chunk> WorldContainer::getChunk(lvec2 pos) const {
	if(m_chunks.count(pos + m_offset)) {
		return m_chunks.at(pos + m_offset);
	}
	return nullptr;
}

void WorldContainer::eraseChunk(lvec2 pos) {
	m_chunks.erase(pos + m_offset);
}

void WorldContainer::setChunkAbsolute(lvec2 pos, const std::shared_ptr<Chunk> &chunk) {
	m_chunks[pos] = chunk;
}

std::shared_ptr<Chunk> WorldContainer::getChunkAbsolute(lvec2 pos) {
	return m_chunks[pos];
}

std::shared_ptr<Chunk> WorldContainer::getChunkAbsolute(lvec2 pos) const {
	if(m_chunks.count(pos)) {
		return m_chunks.at(pos);
	}
	return nullptr;
}

void WorldContainer::eraseChunkAbsolute(lvec2 pos) {
	m_chunks.erase(pos);
}

const std::map<lvec2, std::shared_ptr<Chunk>>& WorldContainer::chunks() const {
	return m_chunks;
}

const std::vector<std::shared_ptr<Entity>>& WorldContainer::entities() const {
	return m_entities;
}

Tile& WorldContainer::at(lvec2 tileoffset) {
	ivec2 chunkpos = tileoffset / ivec2(Chunk::size) - ivec2(tileoffset.x < 0, tileoffset.y < 0);
	ivec2 tilepos = (tileoffset - chunkpos * Chunk::size) % Chunk::size;
	auto chunk = loadChunk(chunkpos + offset());
	return chunk->at(tilepos);
}

Tile WorldContainer::at(lvec2 tileoffset) const {
	ivec2 chunkpos = tileoffset / ivec2(Chunk::size) - ivec2(tileoffset.x < 0, tileoffset.y < 0);
	ivec2 tilepos = (tileoffset - chunkpos * Chunk::size) % Chunk::size;
	auto chunk = getChunk(chunkpos);
	if(chunk) {
		return chunk->at(tilepos);
	}
	return Tile::null;
}

Tile& WorldContainer::operator[](lvec2 tileoffset) {
	return at(tileoffset);
}

Tile WorldContainer::operator[](lvec2 tileoffset) const {
	return at(tileoffset);
}

lvec2 WorldContainer::snapToGrid(vec2 pos) const {
	return lvec2(pos) - lvec2(pos.x < 0 ? 1 : 0, pos.y < 0 ? 1 : 0);
}

lvec2 WorldContainer::getTileIndex(vec2 pixel) const {
	return snapToGrid(pixel / Tile::resolution);
}

void WorldContainer::shift(lvec2 offset) {
	m_offset -= offset;
	for(auto &entity : m_entities) {
		entity->shift(-offset);
	}
}

lvec2 WorldContainer::offset() const {
	return m_offset;
}

Image WorldContainer::renderTileProperties() const {
	Image result(ivec2(Chunk::size * 5), 1);
	for(int cy = -2; cy <= 2; cy++) {
		for(int cx = -2; cx <= 2; cx++) {
			auto chunk = getChunk(lvec2(cx, cy));
			if(chunk) {
				for(int y = 0; y < Chunk::size; y++) {
					for(int x = 0; x < Chunk::size; x++) {
						auto tile = chunk->at(ivec2(x, y));
						uint8_t props =
							tile.visible() +
							(tile.transparent()<<1) +
							(tile.solid()<<2);
						result[ivec2(cx + 2, cy + 2) * Chunk::size + ivec2(x, y)] = props;
					}
				}
			}
		}
	}
	return result;
}

WorldGenerator::WorldGenerator(const WorldContainer &container, vec2 tileScale) : container(container), tileScale(tileScale) {}

std::shared_ptr<Chunk> WorldGenerator::getChunk(lvec2 pos) {
	std::shared_ptr<Chunk> chunk(new Chunk(container, pos, tileScale));

	if(pos.y == -1) {
		for(int x = 0; x < Chunk::size; x++) {
			int groundY = Chunk::size - (sin(float(x) / Chunk::size * pi * 2 - pi * 0.5) * 3 + 3);
			for(int y = groundY, i = 0; y >= 0; y--, i++) {
				if(i < 2) {
					chunk->at(ivec2(x, y)) = Tile(Tile::grass, i);
				}
				else if(i < 12) {
					chunk->at(ivec2(x, y)) = Tile(Tile::dirt, i - 2);
				}
				else if(i < 22) {
					chunk->at(ivec2(x, y)) = Tile(Tile::stone, i - 12);
				}
				else {
					chunk->at(ivec2(x, y)) = Tile(Tile::rock);
				}
			}
		}
	}
	else if(pos.y < -1) {
		chunk->fill(Tile::rock);
	}

	return chunk;
}

WorldRenderer::WorldRenderer(const WorldContainer &container, const Camera &cam, const std::shared_ptr<Entity> &mainEntity, const std::shared_ptr<TiledTexture> &texture)
 : container(container), cam(cam), mainEntity(mainEntity), texture(texture) {
	std::ifstream src("assets/platformer.glsl", std::ios::ate);
	std::string buffer(src.tellg(), '\0');
	src.seekg(src.beg);
	src.read(buffer.data(), buffer.size());

	shader = opengl::Program::load(buffer, opengl::Shader::VertexStage | opengl::Shader::FragmentStage);
	shader.use();
	shader.setUniform("sampler", 0);

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
		{ math::vec3( 0.5f,-0.5f, 0.0f), math::vec2(1,1) },
		{ math::vec3(-0.5f,-0.5f, 0.0f), math::vec2(0,1) },
	});
}

void WorldRenderer::render() {
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

	mat4 transform = mainEntity->getTransform();
	modelInfoUBO.update({transform, mainEntity->getUVTransform()});
	mainEntity->getTexturePtr()->activate();
	unitplane.drawElements(GL_TRIANGLE_STRIP);

	cameraMutex.unlock();

	texture->activate();
	for(auto &[chunkid, chunk] : container.chunks()) {
		lvec2 chunkoffset = chunkid - container.offset();
		vec2 chunkpos = chunkoffset * Chunk::size * Tile::resolution;
		vec2 chunkcenter = (vec2(chunkoffset) + 0.5f) * Chunk::size * Tile::resolution;

		if(dist(campos.xy, chunkcenter) < Chunk::size * Tile::resolution * 1.5) {
			modelInfoUBO.update({mat4().translate(vec3(chunkpos)), mat4()});
			chunk->render();
		}
	}

	for(auto &entity : container.entities()) {
		mat4 transform = entity->getTransform();
		vec2 pos = transform * vec4(0.0f, 0.0f, 0.0f, 1.0f);
		if(dist(campos.xy, pos) < Chunk::size * Tile::resolution * 2) {
			modelInfoUBO.update({transform, entity->getUVTransform()});
			entity->getTexturePtr()->activate();
			unitplane.drawElements(GL_TRIANGLE_STRIP);
		}
	}
}

mat4 WorldRenderer::getCamTransform() {
	std::lock_guard<std::mutex> lock(cameraMutex);
	mat4 transform = cam.proj() * cam.view();
	return transform;
}

std::mutex& WorldRenderer::getCameraMutex() {
	return cameraMutex;
}