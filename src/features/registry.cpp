#include "features/registry.h"

namespace {

// Funcao-local em vez de global: garante que o vetor exista antes do primeiro
// registro, independente da ordem de inicializacao estatica entre .obj.
std::vector<FeatureDef>& Registry() {
	static std::vector<FeatureDef> features;
	return features;
}

} // namespace

void RegisterFeature(const FeatureDef& def) {
	Registry().push_back(def);
}

const std::vector<FeatureDef>& RegisteredFeatures() {
	return Registry();
}
