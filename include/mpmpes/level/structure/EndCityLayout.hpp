#pragma once
// End City jigsaw assembly — port of structure/EndCityLayout.php (hardcoded builder).

#include "mpmpes/level/gen/EndGenerator.hpp" // gen::JavaRandom
#include "mpmpes/level/structure/Repository.hpp"

#include <vector>

namespace mpmpes::level::structure {

class EndCityLayout {
public:
  explicit EndCityLayout(gen::JavaRandom random);
  std::vector<Piece> buildAtRotation(int rotation);

private:
  gen::JavaRandom random_;;
  bool ship_created_ = false;

  Piece add(std::vector<Piece>& pieces, Piece p) { pieces.push_back(std::move(p)); return pieces.back(); }
  Piece makePiece(const std::string& name, int x, int y, int z, int rotation, bool overwrite, int gen_depth = 0);
  Piece relative(const Piece& parent, const std::string& name, int x, int y, int z, int rotation, bool overwrite);
  bool recursiveChildren(const std::string& generator, int depth, const Piece& parent, const int* offset, std::vector<Piece>& pieces);
  bool house(int depth, const Piece& parent, const int* offset, std::vector<Piece>& pieces);
  bool tower(int depth, const Piece& parent, std::vector<Piece>& pieces);
  bool bridge(int depth, const Piece& parent, std::vector<Piece>& pieces);
  bool fatTower(int depth, const Piece& parent, std::vector<Piece>& pieces);
  static const Piece* findCollision(const std::vector<Piece>& pieces, const int box[6]);
};

} // namespace mpmpes::level::structure
