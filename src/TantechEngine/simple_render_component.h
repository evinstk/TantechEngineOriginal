#ifndef TE_SIMPLE_RENDER_COMPONENT_H
#define TE_SIMPLE_RENDER_COMPONENT_H

#include "component.h"
#include <glm/glm.hpp>
#include "gl.h"
#include <memory>

namespace te
{
    struct SimpleRenderInstance
    {
        GLuint vao;
        GLuint vbo;
    };

    class SimpleRenderComponent : public Component<SimpleRenderInstance>
    {
    public:
        SimpleRenderComponent(const glm::mat4& projection);
        ~SimpleRenderComponent();

        void setSprite(const Entity& entity, const glm::vec2& dimensions, const glm::vec2& offset);

        virtual void destroyInstance(const Entity& entity);
    private:
        friend class RenderSystem;

        GLint mShader;
        GLuint mProjectionLocation;
        GLuint mViewLocation;
        GLuint mModelLocation;
    };

    typedef std::shared_ptr<SimpleRenderComponent> SimpleRenderPtr;
}

#endif
