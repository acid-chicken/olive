#include "openglproxy.h"

#include <QThread>

#include "common/clamp.h"
#include "core.h"
#include "node/block/transition/transition.h"
#include "node/node.h"
#include "openglcolorprocessor.h"
#include "openglrenderfunctions.h"
#include "render/colormanager.h"
#include "render/pixelformat.h"

OpenGLProxy::OpenGLProxy(QObject *parent) :
    QObject(parent),
    ctx_(nullptr),
    functions_(nullptr)
{
    surface_.create();
}

OpenGLProxy::~OpenGLProxy()
{
    surface_.destroy();
}

bool OpenGLProxy::Init()
{
    // Create context object
    ctx_ = new QOpenGLContext();

    // Create OpenGL context (automatically destroys any existing if there is one)
    if (!ctx_->create()) {
        qWarning() << "Failed to create OpenGL context in thread" << thread();
        return false;
    }

    ctx_->moveToThread(this->thread());

    // The rest of the initialization needs to occur in the other thread, so we signal for it to start
    QMetaObject::invokeMethod(this, "FinishInit", Qt::QueuedConnection);

    return true;
}

void OpenGLProxy::FrameToValue(DecoderPtr decoder, StreamPtr stream, const TimeRange &range, NodeValueTable* table)
{
    // Ensure stream is video or image type
    if (stream->type() != Stream::kVideo && stream->type() != Stream::kImage) {
        return;
    }

    ImageStreamPtr video_stream = std::static_pointer_cast<ImageStream>(stream);

    // Set up OCIO context
    QString colorspace_match = QStringLiteral("%1:%2").arg(video_stream->footage()->project()->ocio_config(), video_stream->colorspace());

    OpenGLTextureCache::ReferencePtr footage_tex_ref = nullptr;

    if (stream->type() == Stream::kImage && still_image_cache_.Has(stream.get())) {
        CachedStill cs = still_image_cache_.Get(stream.get());

        if (cs.colorspace == colorspace_match
                && cs.alpha_is_associated == video_stream->premultiplied_alpha()
                && cs.divider == video_params_.divider()) {
            footage_tex_ref = cs.texture;
        } else {
            still_image_cache_.Remove(stream.get());
        }
    }

    // Since this is a still image, we could likely optimize this
    if (!footage_tex_ref) {
        OpenGLColorProcessorPtr color_processor = std::static_pointer_cast<OpenGLColorProcessor>(color_cache_.Get(colorspace_match));

        if (!color_processor) {
            color_processor = OpenGLColorProcessor::Create(video_stream->footage()->project()->color_manager()->GetConfig(),
                              video_stream->colorspace(),
                              OCIO::ROLE_SCENE_LINEAR);
            color_cache_.Add(colorspace_match, color_processor);
        }

        ColorManager::OCIOMethod ocio_method = ColorManager::GetOCIOMethodForMode(video_params_.mode());

        FramePtr frame = decoder->RetrieveVideo(range.in(), video_params_.divider());

        // OCIO's CPU conversion is more accurate, so for online we render on CPU but offline we render GPU
        if (ocio_method == ColorManager::kOCIOAccurate) {
            // If alpha is associated, disassociate for the color transform
            if (video_stream->premultiplied_alpha()) {
                ColorManager::DisassociateAlpha(frame);
            }

            // Convert frame to float for OCIO
            frame = PixelFormat::ConvertPixelFormat(frame, PixelFormat::PIX_FMT_RGBA32F);

            // Perform color transform
            color_processor->ConvertFrame(frame);

            // Associate alpha
            if (video_stream->premultiplied_alpha()) {
                ColorManager::ReassociateAlpha(frame);
            } else {
                ColorManager::AssociateAlpha(frame);
            }
        }

        VideoRenderingParams footage_params(frame->width(), frame->height(), stream->timebase(), frame->format(), video_params_.mode());

        footage_tex_ref = texture_cache_.Get(ctx_, footage_params, frame->data());

        if (ocio_method == ColorManager::kOCIOFast) {
            if (!color_processor->IsEnabled()) {
                color_processor->Enable(ctx_, video_stream->premultiplied_alpha());
            }

            // Check frame aspect ratio
            if (frame->sample_aspect_ratio() != 1 && frame->sample_aspect_ratio() != 0) {
                int new_width = frame->width();
                int new_height = frame->height();

                // Scale the frame in a way that does not reduce the resolution
                if (frame->sample_aspect_ratio() > 1) {
                    // Make wider
                    new_width = qRound(static_cast<double>(new_width) * frame->sample_aspect_ratio().toDouble());
                } else {
                    // Make taller
                    new_height = qRound(static_cast<double>(new_height) / frame->sample_aspect_ratio().toDouble());
                }

                footage_params = VideoRenderingParams(new_width,
                                                      new_height,
                                                      footage_params.time_base(),
                                                      footage_params.format(),
                                                      footage_params.mode());
            }

            // Create destination texture
            OpenGLTextureCache::ReferencePtr associated_tex_ref = texture_cache_.Get(ctx_, footage_params);

            buffer_.Attach(associated_tex_ref->texture(), true);
            buffer_.Bind();
            footage_tex_ref->texture()->Bind();

            // Set viewport for texture size
            functions_->glViewport(0, 0, associated_tex_ref->texture()->width(), associated_tex_ref->texture()->height());

            // Blit old texture to new texture through OCIO shader
            color_processor->ProcessOpenGL();

            footage_tex_ref->texture()->Release();
            buffer_.Release();
            buffer_.Detach();

            footage_tex_ref = associated_tex_ref;
        }

        if (stream->type() == Stream::kImage) {
            still_image_cache_.Add(stream.get(), {footage_tex_ref, colorspace_match, video_stream->premultiplied_alpha(), video_params_.divider()});
        }
    }

    table->Push(NodeParam::kTexture, QVariant::fromValue(footage_tex_ref));
}

void OpenGLProxy::Close()
{
    shader_cache_.Clear();
    buffer_.Destroy();
    functions_ = nullptr;
    delete ctx_;
}

void OpenGLProxy::RunNodeAccelerated(const Node *node, const TimeRange &range, const NodeValueDatabase &input_params, NodeValueTable *output_params)
{
    if (!node->IsAccelerated()) {
        return;
    }

    OpenGLShaderPtr shader = shader_cache_.Get(node->id());

    if (!shader) {
        // Since we have shader code, compile it now

        QString frag_code = node->AcceleratedCodeFragment();
        QString vert_code = node->AcceleratedCodeVertex();

        if (frag_code.isEmpty()) {
            frag_code = OpenGLShader::CodeDefaultFragment();
        }

        if (vert_code.isEmpty()) {
            vert_code = OpenGLShader::CodeDefaultVertex();
        }

        shader = std::make_shared<OpenGLShader>();
        shader->create();
        shader->addShaderFromSourceCode(QOpenGLShader::Fragment, frag_code);
        shader->addShaderFromSourceCode(QOpenGLShader::Vertex, vert_code);
        shader->link();

        shader_cache_.Add(node->id(), shader);
    }

    // Create the output textures
    QList<OpenGLTextureCache::ReferencePtr> dst_refs;
    dst_refs.append(texture_cache_.Get(ctx_, video_params_));
    GLuint iterative_input = 0;

    // If this node requires multiple iterations, get a texture for it too
    if (node->AcceleratedCodeIterations() > 1 && node->AcceleratedCodeIterativeInput()) {
        dst_refs.append(texture_cache_.Get(ctx_, video_params_));
    }

    // Lock the shader so no other thread interferes as we set parameters and draw (and we don't interfere with any others)
    shader->bind();

    unsigned int input_texture_count = 0;

    foreach (NodeParam* param, node->parameters()) {
        if (param->type() == NodeParam::kInput) {
            // See if the shader has takes this parameter as an input
            int variable_location = shader->uniformLocation(param->id());

            if (variable_location > -1) {
                // This variable is used in the shader, let's set it to our value

                NodeInput* input = static_cast<NodeInput*>(param);

                // Get value from database at this input
                const NodeValueTable& input_data = input_params[input];

                QVariant value = node->InputValueFromTable(input, input_data);

                switch (input->data_type()) {
                case NodeInput::kInt:
                    shader->setUniformValue(variable_location, value.toInt());
                    break;
                case NodeInput::kFloat:
                    shader->setUniformValue(variable_location, value.toFloat());
                    break;
                case NodeInput::kVec2:
                    shader->setUniformValue(variable_location, value.value<QVector2D>());
                    break;
                case NodeInput::kVec3:
                    shader->setUniformValue(variable_location, value.value<QVector3D>());
                    break;
                case NodeInput::kVec4:
                    shader->setUniformValue(variable_location, value.value<QVector4D>());
                    break;
                case NodeInput::kMatrix:
                    shader->setUniformValue(variable_location, value.value<QMatrix4x4>());
                    break;
                case NodeInput::kColor:
                    shader->setUniformValue(variable_location, value.value<QColor>());
                    break;
                case NodeInput::kBoolean:
                    shader->setUniformValue(variable_location, value.toBool());
                    break;
                case NodeInput::kFootage:
                case NodeInput::kTexture:
                case NodeInput::kBuffer:
                {
                    OpenGLTextureCache::ReferencePtr texture = value.value<OpenGLTextureCache::ReferencePtr>();

                    functions_->glActiveTexture(GL_TEXTURE0 + input_texture_count);

                    GLuint tex_id = texture ? texture->texture()->texture() : 0;
                    functions_->glBindTexture(GL_TEXTURE_2D, tex_id);

                    // Set value to bound texture
                    shader->setUniformValue(variable_location, input_texture_count);

                    // Set enable flag if shader wants it
                    int enable_param_location = shader->uniformLocation(QStringLiteral("%1_enabled").arg(input->id()));
                    if (enable_param_location > -1) {
                        shader->setUniformValue(enable_param_location,
                                                tex_id > 0);
                    }

                    if (tex_id > 0) {
                        // Set texture resolution if shader wants it
                        int res_param_location = shader->uniformLocation(QStringLiteral("%1_resolution").arg(input->id()));
                        if (res_param_location > -1) {
                            shader->setUniformValue(res_param_location,
                                                    static_cast<GLfloat>(texture->texture()->width() * video_params_.divider()),
                                                    static_cast<GLfloat>(texture->texture()->height() * video_params_.divider()));
                        }
                    }

                    // If this texture binding is the iterative input, set it here
                    if (input == node->AcceleratedCodeIterativeInput()) {
                        iterative_input = input_texture_count;
                    }

                    OpenGLRenderFunctions::PrepareToDraw(functions_);

                    input_texture_count++;
                    break;
                }
                case NodeInput::kSamples:
                case NodeInput::kText:
                case NodeInput::kRational:
                case NodeInput::kFont:
                case NodeInput::kFile:
                case NodeInput::kDecimal:
                case NodeInput::kWholeNumber:
                case NodeInput::kNumber:
                case NodeInput::kString:
                case NodeInput::kVector:
                case NodeInput::kNone:
                case NodeInput::kAny:
                    break;
                }
            }
        }
    }

    // Set up OpenGL parameters as necessary
    functions_->glViewport(0, 0, video_params_.effective_width(), video_params_.effective_height());

    // Provide some standard args
    shader->setUniformValue("ove_resolution",
                            static_cast<GLfloat>(video_params_.width()),
                            static_cast<GLfloat>(video_params_.height()));

    if (node->IsBlock() && static_cast<const Block*>(node)->type() == Block::kTransition) {
        const TransitionBlock* transition_node = static_cast<const TransitionBlock*>(node);

        // Provides total transition progress from 0.0 (start) - 1.0 (end)
        shader->setUniformValue("ove_tprog_all", static_cast<GLfloat>(transition_node->GetTotalProgress(range.in())));

        // Provides progress of out section from 1.0 (start) - 0.0 (end)
        shader->setUniformValue("ove_tprog_out", static_cast<GLfloat>(transition_node->GetOutProgress(range.in())));

        // Provides progress of in section from 0.0 (start) - 1.0 (end)
        shader->setUniformValue("ove_tprog_in", static_cast<GLfloat>(transition_node->GetInProgress(range.in())));
    }

    // Some nodes use multiple iterations for optimization
    OpenGLTextureCache::ReferencePtr output_tex;

    for (int iteration=0; iteration<node->AcceleratedCodeIterations(); iteration++) {
        // If this is not the first iteration, set the parameter that will receive the last iteration's texture
        OpenGLTextureCache::ReferencePtr source_tex = dst_refs.at((iteration+1)%dst_refs.size());
        OpenGLTextureCache::ReferencePtr destination_tex = dst_refs.at(iteration%dst_refs.size());

        // Set iteration number
        shader->bind();
        shader->setUniformValue("ove_iteration", iteration);
        shader->release();

        if (iteration > 0) {
            functions_->glActiveTexture(GL_TEXTURE0 + iterative_input);
            functions_->glBindTexture(GL_TEXTURE_2D, source_tex->texture()->texture());
        }

        buffer_.Attach(destination_tex->texture(), true);
        buffer_.Bind();

        // Blit this texture through this shader
        OpenGLRenderFunctions::Blit(shader);

        buffer_.Release();
        buffer_.Detach();

        // Update output reference to the last texture we wrote to
        output_tex = destination_tex;
    }

    // Release any textures we bound before
    while (input_texture_count > 0) {
        input_texture_count--;

        // Release texture here
        functions_->glActiveTexture(GL_TEXTURE0 + input_texture_count);
        functions_->glBindTexture(GL_TEXTURE_2D, 0);
    }

    shader->release();

    output_params->Push(NodeParam::kTexture, QVariant::fromValue(output_tex));
}

void OpenGLProxy::TextureToBuffer(const QVariant &tex_in, void *buffer)
{
    OpenGLTextureCache::ReferencePtr texture = tex_in.value<OpenGLTextureCache::ReferencePtr>();

    QOpenGLFunctions* f = QOpenGLContext::currentContext()->functions();
    buffer_.Attach(texture->texture());
    buffer_.Bind();

    f->glReadPixels(0,
                    0,
                    video_params_.effective_width(),
                    video_params_.effective_height(),
                    OpenGLRenderFunctions::GetPixelFormat(video_params_.format()),
                    OpenGLRenderFunctions::GetPixelType(video_params_.format()),
                    buffer);

    buffer_.Release();
    buffer_.Detach();
}

void OpenGLProxy::SetParameters(const VideoRenderingParams &params)
{
    video_params_ = params;

    if (functions_ != nullptr && video_params_.is_valid()) {
        functions_->glViewport(0, 0, video_params_.effective_width(), video_params_.effective_height());
    }
}

void OpenGLProxy::FinishInit()
{
    // Make context current on that surface
    if (!ctx_->makeCurrent(&surface_)) {
        qWarning() << "Failed to makeCurrent() on offscreen surface in thread" << thread();
        return;
    }

    // Store OpenGL functions instance
    functions_ = ctx_->functions();
    functions_->glBlendFunc(GL_ONE, GL_ZERO);

    SetParameters(video_params_);

    buffer_.Create(ctx_);
}
